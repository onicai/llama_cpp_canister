// Typed decision models: run_decision.
//
// A port of the laya and kev paths of llama.cpp
// tools/server/server-decision.cpp (b11476), keeping its function names (init,
// parse, render, render_options, fill_task_laya, decision_kev_render,
// decision_kev_text, get_temperature, format_answer) so it can be diffed on each
// llama.cpp upgrade. Left out: images and the other decision types (they need
// models too large for a canister).
//
// laya (Julia-1, Laya) is a bidirectional encoder: one forward pass over the
// whole prompt per question, which must fit in one update call. kev (Kev-0.8B)
// is causal and its prompt starts with the state, so the state is a prefix: it
// is ingested over as many calls as needed into a per-principal session file
// (the same mechanism as run_update's prompt cache), and every question then
// only decodes its own tokens on top of it. A later request about the same
// state reuses the stored one.
//
// Two canister rules shape the code (see README-contributors-guide.md):
// - A C++ throw traps the canister. Upstream throws on every invalid input; here
//   every input check returns an error message instead, and common_json values
//   are type-checked before get<T>() (which throws on a mismatch).
// - The decision template is NOT built with common_chat_template: its
//   constructor runs jinja::caps_get(), which executes the template with dummy
//   chat inputs and relies on catching the resulting exception.
//
// A request can need several update calls: each call answers the pending
// questions that fit the per-call token budget (max_tokens_update, the same
// setting that protects run_update) and returns the answers so far plus the ids
// still pending. The answers so far are kept in a per-principal file next to
// the prompt caches, so the cache cleanup timer covers them.

#include "decision.h"

#include "auth.h"
#include "main_.h"
#include "max_tokens.h"
#include "promptcache.h"
#include "utils.h"

// "common/" prefix: a bare "common.h" resolves to ggml/src/ggml-cpu/common.h,
// which comes earlier on the include path (same for unicode.h in utils.cpp)
#include "common/common.h"
#include "common/jinja/lexer.h"
#include "common/jinja/parser.h"
#include "common/jinja/runtime.h"
#include "common/json.h"
#include "llama.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <vector>

// The IC's instruction counter, for the canister log. 0 in native builds.
#ifdef __wasm__
extern "C" uint64_t ic0_performance_counter(uint32_t counter_type)
    __attribute__((import_module("ic0"), import_name("performance_counter")));
static uint64_t instruction_counter() { return ic0_performance_counter(0); }
#else
static uint64_t instruction_counter() { return 0; }
#endif

namespace {

void log_line(const std::string &msg) {
  std::cout << "llama_cpp: run_decision - " << msg << std::endl;
}

std::string billions(uint64_t n) {
  std::ostringstream oss;
  oss.precision(2);
  oss << std::fixed << n / 1e9 << " B";
  return oss.str();
}

enum decision_question_type {
  DECISION_QUESTION_CHOICE, // the order is the column of the laya head output
  DECISION_QUESTION_SCORE,
  DECISION_QUESTION_NOUL,
};

const char *decision_question_type_name(decision_question_type type) {
  switch (type) {
  case DECISION_QUESTION_CHOICE:
    return "choice";
  case DECISION_QUESTION_SCORE:
    return "score";
  case DECISION_QUESTION_NOUL:
    return "noul";
  }
  return "";
}

struct decision_option {
  std::string key;
  std::string description; // empty: not provided (null for the template)
};

struct decision_question {
  std::string id;
  decision_question_type type;
  std::string instructions;
  std::vector<decision_option> options;  // in the order of the model outputs
  std::vector<std::string> option_names; // the request's key of each option
};

struct decision_answer {
  std::string id;
  decision_question_type type;
  std::string choice;
  double score = 0.0;
  double yes = 0.0;
  double confidence = 0.0;
  std::vector<std::pair<std::string, double>> probabilities;
};

// The decision setup of the loaded model. Lives in the Orthogonally Persisted
// heap together with the model; rebuilt by decision_init on every load_model.
struct decision_context {
  common_decision_type type = COMMON_DECISION_TYPE_NONE;
  std::string error; // why a decision model cannot be used, if it cannot

  const llama_vocab *vocab = nullptr;
  std::string tmpl_source;
  std::shared_ptr<jinja::program> tmpl; // the "systemone" template

  std::map<std::string, float> temperatures; // "<type>" or "<type>.<bucket>"
  size_t n_options_max = 0;

  llama_token token_marker = LLAMA_TOKEN_NULL;
  llama_token token_sep = LLAMA_TOKEN_NULL;
  std::string text_marker;
  size_t max_head_tokens = 0; // question + options
  size_t max_option_tokens = 48;

  // kev: the state is a prefix that ends at token_state_end; the scores are
  // the dot product of a q (at the last token) and a k (at each marker), each
  // n_pointer wide
  llama_token token_state_end = LLAMA_TOKEN_NULL;
  int32_t n_pointer = 0;
};

decision_context g_decision;

std::string decision_meta_str(const llama_model *model,
                              const std::string &key) {
  char buf[256];
  const int32_t n =
      llama_model_meta_val_str(model, key.c_str(), buf, sizeof(buf));
  return n < 0 ? "" : std::string(buf);
}

// The token of a special token's text, LLAMA_TOKEN_NULL if it is not one token
llama_token single_token(const llama_vocab *vocab, const std::string &text) {
  const std::vector<llama_token> toks =
      common_tokenize(vocab, text, false, true);
  return toks.size() == 1 ? toks[0] : LLAMA_TOKEN_NULL;
}

//
// model-specific setup (upstream: server_decision_context::init)
//

void init(decision_context &d, const llama_model *model) {
  d = decision_context(); // the model can be reloaded

  const common_decision_type model_type = common_get_decision_type(model);
  if (model_type == COMMON_DECISION_TYPE_NONE) {
    return;
  }
  d.type = model_type;

  const std::string prefix =
      decision_meta_str(model, "general.architecture") + ".decision.";
  const std::string type_name = decision_meta_str(model, prefix + "type");

  if (model_type != COMMON_DECISION_TYPE_LAYA &&
      model_type != COMMON_DECISION_TYPE_KEV) {
    d.error = "decision model type '" + type_name +
              "' is not supported by llama_cpp_canister (supported: laya, "
              "e.g. Julia-1 and Laya, and kev, e.g. Kev-0.8B)";
    return;
  }

  d.vocab = llama_model_get_vocab(model);

  const char *tmpl_src = llama_model_chat_template(model, "systemone");
  if (tmpl_src == nullptr) {
    d.error = "decision model has no \"systemone\" template";
    return;
  }
  jinja::lexer lexer;
  jinja::lexer_result lexer_res = lexer.tokenize(tmpl_src);
  d.tmpl =
      std::make_shared<jinja::program>(jinja::parse_from_tokens(lexer_res));
  d.tmpl_source = lexer_res.source;

  const std::string prefix_temp = prefix + "temperature.";
  for (int32_t i = 0; i < llama_model_meta_count(model); i++) {
    char key[256];
    char val[64];
    if (llama_model_meta_key_by_index(model, i, key, sizeof(key)) < 0 ||
        !string_starts_with(key, prefix_temp)) {
      continue;
    }
    if (llama_model_meta_val_str_by_index(model, i, val, sizeof(val)) < 0) {
      continue;
    }
    const float temp = std::strtof(val, nullptr);
    if (temp <= 0.0f) {
      d.error =
          std::string("invalid decision temperature: ") + key + " = " + val;
      return;
    }
    d.temperatures[key + prefix_temp.size()] = temp;
  }

  if (model_type == COMMON_DECISION_TYPE_KEV) {
    // the hidden state of an option is read at the token that ends it
    d.token_marker = single_token(d.vocab, "<|box_end|>");
    d.token_state_end = single_token(d.vocab, "<|fim_middle|>");
    if (d.token_marker == LLAMA_TOKEN_NULL ||
        d.token_state_end == LLAMA_TOKEN_NULL) {
      d.error = "decision model has no <|box_end|> or <|fim_middle|> token";
      return;
    }
    const int32_t n_embd_out = llama_model_n_embd_out(model);
    if (n_embd_out % 2 != 0 || n_embd_out == llama_model_n_embd(model)) {
      d.error = "decision model has no pointer head";
      return;
    }
    d.n_pointer = n_embd_out / 2;
  } else {
    d.token_marker = llama_vocab_mask(d.vocab);
    d.token_sep = llama_vocab_sep(d.vocab);
    if (d.token_marker == LLAMA_TOKEN_NULL || d.token_sep == LLAMA_TOKEN_NULL) {
      d.error = "decision model has no mask or sep token";
      return;
    }
    d.text_marker = common_token_to_piece(d.vocab, d.token_marker, true);

    d.max_head_tokens = std::strtoul(
        decision_meta_str(model, prefix + "max_head_tokens").c_str(), nullptr,
        10);
    if (d.max_head_tokens == 0) {
      d.error = "decision model has no valid max_head_tokens";
      return;
    }
  }
  d.n_options_max = 255;

  std::cout << "llama_cpp: decision_init - decision model '"
            << decision_meta_str(model, "general.name") << "', type "
            << type_name << ", systemone template, " << d.temperatures.size()
            << " softmax temperatures"
            << (d.max_head_tokens
                    ? ", max_head_tokens " + std::to_string(d.max_head_tokens)
                    : ", pointer head " + std::to_string(d.n_pointer))
            << std::endl;
}

//
// request parsing (upstream: parse_questions, from the flat Candid tables)
//

struct decision_request {
  std::string state_label; // "Text" or "Json"
  std::string state_text;
  std::string state_json;
  std::vector<std::string> q_ids;
  std::vector<std::string> q_kinds;
  std::vector<std::string> q_instructions;
  std::vector<std::string> o_question_ids;
  std::vector<std::string> o_keys;
  std::vector<std::string> o_descriptions;
};

// Returns an error message, empty when the request is valid.
std::string parse(const decision_context &d, const decision_request &req,
                  common_json &state,
                  std::vector<decision_question> &questions) {
  if (req.state_label == "Json") {
    state = common_json::parse_no_throw(req.state_json);
    if (state.is_discarded()) {
      return "state: Json is not valid JSON";
    }
    if (state.is_null()) {
      return "state: must not be null";
    }
  } else {
    state = common_json(req.state_text);
  }

  if (req.q_ids.empty()) {
    return "questions: must not be empty";
  }
  std::map<std::string, size_t> index;
  for (size_t i = 0; i < req.q_ids.size(); i++) {
    const std::string &id = req.q_ids[i];
    if (id.empty()) {
      return "questions: id must not be empty";
    }
    if (index.count(id)) {
      return "questions." + id + ": duplicate id";
    }
    index[id] = i;
    decision_question q;
    q.id = id;
    q.instructions = req.q_instructions[i];
    const std::string &kind = req.q_kinds[i];
    q.type = kind == "choice"  ? DECISION_QUESTION_CHOICE
             : kind == "score" ? DECISION_QUESTION_SCORE
                               : DECISION_QUESTION_NOUL;
    questions.push_back(q);
  }

  // the option rows, per question, in request order
  std::vector<std::vector<decision_option>> rows(questions.size());
  for (size_t j = 0; j < req.o_keys.size(); j++) {
    const auto it = index.find(req.o_question_ids[j]);
    if (it == index.end()) {
      return "options: question_id '" + req.o_question_ids[j] +
             "' is not a question id";
    }
    rows[it->second].push_back({req.o_keys[j], req.o_descriptions[j]});
  }

  for (size_t i = 0; i < questions.size(); i++) {
    decision_question &q = questions[i];
    const std::vector<decision_option> &opts = rows[i];
    const std::string err = "questions." + q.id + ": ";
    std::set<std::string> keys;
    for (const auto &o : opts) {
      if (o.key.empty()) {
        return err + "an option key must not be empty";
      }
      if (!keys.insert(o.key).second) {
        return err + "duplicate option key '" + o.key + "'";
      }
    }

    if (q.type == DECISION_QUESTION_CHOICE) {
      if (opts.empty()) {
        return err + "a choice question needs at least one option";
      }
      for (const auto &o : opts) {
        q.options.push_back(o);
        q.option_names.push_back(o.key);
      }
    } else if (q.type == DECISION_QUESTION_SCORE) {
      if (opts.size() < 2 || opts.size() > 10) {
        return err + "a score question needs 2 to 10 levels";
      }
      // upstream: the levels are an array, keys are their index and the level
      // text is the description
      for (size_t l = 0; l < opts.size(); l++) {
        q.options.push_back({std::to_string(l), opts[l].description.empty()
                                                    ? opts[l].key
                                                    : opts[l].description});
        q.option_names.push_back(opts[l].key);
      }
    } else {
      // noul: options are [false, true]; descriptions are optional rows
      std::string desc_false, desc_true;
      for (const auto &o : opts) {
        if (o.key == "false") {
          desc_false = o.description;
        } else if (o.key == "true") {
          desc_true = o.description;
        } else {
          return err + "a noul option key must be 'true' or 'false'";
        }
      }
      q.options = {{"false", desc_false}, {"true", desc_true}};
      q.option_names = {"false", "true"};
    }

    if (q.options.size() > d.n_options_max) {
      return err + "too many options (" + std::to_string(q.options.size()) +
             "), this model supports at most " +
             std::to_string(d.n_options_max);
    }
  }
  return "";
}

//
// prompt
//

// replace text in all strings of a JSON value
common_json decision_replace_text(const common_json &val,
                                  const std::string &search,
                                  const std::string &replace) {
  if (val.is_string()) {
    std::string str = val.get<std::string>();
    string_replace_all(str, search, replace);
    return common_json(str);
  }
  if (val.is_array()) {
    common_json out = common_json::array();
    for (auto it = val.begin(); it != val.end(); ++it) {
      out.push_back(decision_replace_text(it.value(), search, replace));
    }
    return out;
  }
  if (val.is_object()) {
    common_json out = common_json::object();
    for (const auto &[key, item] : val.items()) {
      out[key] = decision_replace_text(item, search, replace);
    }
    return out;
  }
  return val;
}

// kev flattens a JSON value into text, the keys of an object are kept as
// labels (kev/api.py: render)
std::string decision_kev_render(const common_json &val, int indent = 0) {
  const std::string pad(2 * indent, ' ');
  if (val.is_null()) {
    return "";
  }
  if (val.is_string()) {
    return val.get<std::string>();
  }
  if (val.is_boolean()) {
    return val.get<bool>() ? "True" : "False";
  }
  if (val.is_array()) {
    std::string out;
    for (auto it = val.begin(); it != val.end(); ++it) {
      const std::string text = decision_kev_render(it.value(), indent + 1);
      out +=
          (out.empty() ? "" : "\n") + pad + "- " +
          text.substr(std::min(text.size(), text.find_first_not_of(" \t\n\r")));
    }
    return out;
  }
  if (val.is_object()) {
    std::string out;
    for (const auto &[key, item] : val.items()) {
      const bool is_nested = item.is_object() || item.is_array();
      out += (out.empty() ? "" : "\n") + pad + key +
             (is_nested ? ":\n" : ": ") +
             decision_kev_render(item, is_nested ? indent + 1 : 0);
    }
    return out;
  }
  return val.dump();
}

// kev text input: special tokens written in the text must not be parsed as
// such. Upstream replaces <\|([A-Za-z0-9_]+)\|> by <\xC2\xA6$1\xC2\xA6> with
// std::regex; this scan does the same without regex (recursion, stack depth).
std::string decision_kev_text(const common_json &val) {
  const std::string text = decision_kev_render(val);
  const std::string bar = "\xC2\xA6";
  std::string out;
  size_t i = 0;
  while (i < text.size()) {
    if (text.compare(i, 2, "<|") == 0) {
      size_t j = i + 2;
      while (j < text.size() &&
             (std::isalnum((unsigned char)text[j]) || text[j] == '_')) {
        j++;
      }
      if (j > i + 2 && text.compare(j, 2, "|>") == 0) {
        out += "<" + bar + text.substr(i + 2, j - i - 2) + bar + ">";
        i = j + 2;
        continue;
      }
    }
    out += text[i++];
  }
  return out;
}

common_json render_options(const decision_context &d,
                           const decision_question &question) {
  const bool kev = d.type == COMMON_DECISION_TYPE_KEV;
  common_json options = common_json::array();
  for (const auto &opt : question.options) {
    common_json option = common_json::object();
    option["key"] = kev ? common_json(decision_kev_text(common_json(opt.key)))
                        : common_json(opt.key);
    option["description"] =
        opt.description.empty()
            ? common_json(nullptr)
            : common_json(kev ? decision_kev_text(common_json(opt.description))
                              : opt.description);
    options.push_back(option);
  }
  return options;
}

std::string render(const decision_context &d, const common_json &state,
                   const decision_question &question) {
  // the template is given raw JSON values, it serializes the ones that are not
  // strings
  common_json inp = common_json::object();
  inp["id"] = common_json(question.id);
  inp["type"] = common_json(decision_question_type_name(question.type));
  inp["instructions"] = common_json(question.instructions);
  inp["state"] = state;
  inp["options"] = render_options(d, question);

  // the kev template only takes text
  if (d.type == COMMON_DECISION_TYPE_KEV) {
    inp["state"] = common_json(decision_kev_text(state));
    inp["instructions"] =
        common_json(decision_kev_text(common_json(question.instructions)));
  }

  // the input must not contain the marker of the options
  if (!d.text_marker.empty()) {
    inp = decision_replace_text(inp, d.text_marker, " ");
  }
  inp["images"] = common_json::array();

  jinja::context ctx(d.tmpl_source);
  jinja::global_from_json(ctx, inp, false);
  jinja::runtime runtime(ctx);
  const jinja::value results = runtime.execute(*d.tmpl);
  return jinja::runtime::gather_string_parts(results)->as_string().str();
}

// the prompt is: [cls] question [sep] ([marker] option)* [sep] state [sep]
// options and question are cut to fit max_head_tokens, the same way the model
// was trained. Returns an error message, empty on success.
std::string fill_task_laya(const decision_context &d,
                           std::vector<llama_token> &tokens,
                           const decision_question &question,
                           std::vector<int32_t> &out_markers) {
  const size_t n_options = question.options.size();

  std::vector<size_t> markers;
  for (size_t i = 0; i < tokens.size(); i++) {
    if (tokens[i] == d.token_marker) {
      markers.push_back(i);
    }
  }
  const std::string invalid = "unexpected layout of the decision prompt";
  if (markers.size() != n_options || markers[0] < 2 ||
      tokens[markers[0] - 1] != d.token_sep || tokens.back() != d.token_sep) {
    return invalid;
  }
  const size_t head_end = markers[0] - 1;
  const size_t opts_end =
      std::find(tokens.begin() + markers.back(), tokens.end(), d.token_sep) -
      tokens.begin();
  if (opts_end + 1 >= tokens.size()) {
    return invalid;
  }

  // marker + text of each option
  std::vector<std::vector<llama_token>> options;
  size_t n_options_tokens = 0;
  auto set_max = [&](size_t n_max) {
    n_options_tokens = 0;
    for (auto &opt : options) {
      opt.resize(std::min(opt.size(), n_max));
      n_options_tokens += opt.size();
    }
  };
  for (size_t i = 0; i < n_options; i++) {
    const size_t end = i + 1 < n_options ? markers[i + 1] : opts_end;
    options.emplace_back(tokens.begin() + markers[i], tokens.begin() + end);
  }
  set_max(d.max_option_tokens + 1);
  if (n_options_tokens + 16 > d.max_head_tokens) {
    // too many or too long options, shrink them evenly
    set_max(std::max((size_t)4, (d.max_head_tokens -
                                 std::min(d.max_head_tokens, (size_t)16)) /
                                    n_options));
  }
  const size_t n_question_max =
      std::max((size_t)8, d.max_head_tokens -
                              std::min(d.max_head_tokens, n_options_tokens));

  std::vector<llama_token> out;
  out.push_back(tokens[0]);
  out.insert(out.end(), tokens.begin() + 1,
             tokens.begin() + std::min(head_end, 1 + n_question_max));
  out.push_back(d.token_sep);
  for (const auto &opt : options) {
    out_markers.push_back(out.size());
    out.insert(out.end(), opt.begin(), opt.end());
  }
  out.insert(out.end(), tokens.begin() + opts_end, tokens.end());
  tokens = std::move(out);
  return "";
}

// the prompt is: [fim_prefix] state [fim_middle] instructions
// ([box_start] option [box_end])* [fim_suffix]. An option is read at its end
// token, the question at the last token. Returns an error message, empty on
// success.
std::string fill_task_kev(const decision_context &d,
                          const std::vector<llama_token> &tokens,
                          const decision_question &question,
                          std::vector<int32_t> &out_markers,
                          int32_t &out_pointer, size_t &out_state_end) {
  const auto state_end =
      std::find(tokens.begin(), tokens.end(), d.token_state_end);
  if (state_end == tokens.end()) {
    return "unexpected layout of the decision prompt";
  }
  out_state_end = state_end - tokens.begin();
  for (size_t i = out_state_end; i < tokens.size(); i++) {
    if (tokens[i] == d.token_marker) {
      out_markers.push_back(i);
    }
  }
  if (out_markers.size() != question.options.size()) {
    return "unexpected layout of the decision prompt";
  }
  out_pointer = tokens.size() - 1;
  return "";
}

// Decodes tokens[begin, end) at positions pos0 + i without outputs: the state
// prefix of kev, which only has to reach the memory. Returns an error message.
std::string decode_prefix(llama_context *ctx,
                          const std::vector<llama_token> &tokens, size_t begin,
                          size_t end) {
  llama_batch batch = llama_batch_init(end - begin, 0, 1);
  for (size_t i = begin; i < end; i++) {
    const size_t b = i - begin;
    batch.token[b] = tokens[i];
    batch.pos[b] = i;
    batch.n_seq_id[b] = 1;
    batch.seq_id[b][0] = 0;
    batch.logits[b] = false;
  }
  batch.n_tokens = end - begin;
  const int32_t rc = llama_decode(ctx, batch);
  llama_batch_free(batch);
  return rc == 0 ? ""
                 : "llama_decode of the state failed with code " +
                       std::to_string(rc);
}

// One forward pass over tokens, at positions pos0 + i. The score of each
// option is read at its marker: the embeddings output in the column of the
// question type (laya), or the scaled dot product of the q at the pointer with
// the k at the marker (kev, pointer >= 0). Returns an error message.
std::string decode(llama_context *ctx, const decision_context &d,
                   const std::vector<llama_token> &tokens, llama_pos pos0,
                   const std::vector<int32_t> &markers, int32_t column,
                   int32_t pointer, std::vector<float> &scores) {
  llama_batch batch = llama_batch_init(tokens.size(), 0, 1);
  for (size_t i = 0; i < tokens.size(); i++) {
    batch.token[i] = tokens[i];
    batch.pos[i] = pos0 + i;
    batch.n_seq_id[i] = 1;
    batch.seq_id[i][0] = 0;
    // every token is an output: llama.cpp requires that for an embeddings
    // context anyway (it overrides a partial selection, with a warning)
    batch.logits[i] = true;
  }
  batch.n_tokens = tokens.size();
  const int32_t rc = llama_decode(ctx, batch);
  llama_batch_free(batch);
  if (rc != 0) {
    return "llama_decode failed with code " + std::to_string(rc);
  }
  const float *embd_q =
      pointer >= 0 ? llama_get_embeddings_ith(ctx, pointer) : nullptr;
  if (pointer >= 0 && embd_q == nullptr) {
    return "failed to get the embeddings of the question";
  }
  for (const int32_t marker : markers) {
    const float *embd = llama_get_embeddings_ith(ctx, marker);
    if (embd == nullptr) {
      return "failed to get the embeddings of an option";
    }
    if (pointer < 0) {
      scores.push_back(embd[column]);
      continue;
    }
    float dot = 0.0f;
    for (int32_t i = 0; i < d.n_pointer; i++) {
      dot += embd_q[i] * embd[d.n_pointer + i];
    }
    scores.push_back(dot / sqrtf((float)d.n_pointer));
  }
  return "";
}

//
// answer
//

float get_temperature(const decision_context &d,
                      const decision_question &question) {
  const size_t n = question.options.size();
  const std::string type_name = decision_question_type_name(question.type);

  // the temperature can depend on the number of options, the buckets are the
  // ones used to fit it
  const std::string bucket = n <= 2    ? "2"
                             : n <= 5  ? "3_5"
                             : n <= 10 ? "6_10"
                                       : "11";
  for (const auto &name : {type_name + "." + bucket, type_name}) {
    const auto it = d.temperatures.find(name);
    if (it != d.temperatures.end()) {
      return it->second;
    }
  }
  return 1.0f;
}

// confidence formulas are the ones published by TypeSafe

double decision_confidence_choice(const std::vector<double> &probs) {
  if (probs.size() < 2) {
    return 1.0;
  }
  const double uniform = 1.0 / probs.size();
  const double p_max = *std::max_element(probs.begin(), probs.end());
  return std::max(0.0, (p_max - uniform) / (1.0 - uniform));
}

double decision_confidence_score(const std::vector<double> &probs) {
  if (probs.size() < 2) {
    return 1.0;
  }
  const size_t n = probs.size();
  const size_t mode =
      std::max_element(probs.begin(), probs.end()) - probs.begin();

  // mean distance to the mode, relative to the one of a uniform distribution
  // around its center
  double dist = 0.0;
  double dist_uniform = 0.0;
  for (size_t i = 0; i < n; i++) {
    dist += probs[i] * std::fabs((double)i - (double)mode);
    dist_uniform += std::fabs((double)i - (n - 1) / 2.0) / n;
  }
  return std::max(0.0, 1.0 - dist / dist_uniform);
}

decision_answer format_answer(const decision_context &d,
                              const decision_question &question,
                              const std::vector<float> &scores) {
  const size_t n = scores.size();

  // softmax over the outputs (laya has one variant)
  const float temperature = get_temperature(d, question);
  const float score_max = *std::max_element(scores.begin(), scores.end());
  std::vector<double> probs(n);
  double sum = 0.0;
  for (size_t i = 0; i < n; i++) {
    probs[i] = std::exp((double)(scores[i] - score_max) / temperature);
    sum += probs[i];
  }
  for (auto &p : probs) {
    p /= sum;
  }

  decision_answer answer;
  answer.id = question.id;
  answer.type = question.type;
  for (size_t i = 0; i < n; i++) {
    answer.probabilities.push_back({question.option_names[i], probs[i]});
  }

  if (question.type == DECISION_QUESTION_NOUL) {
    for (size_t i = 0; i < n; i++) {
      if (question.options[i].key == "true") {
        answer.yes = probs[i];
      }
    }
  } else if (question.type == DECISION_QUESTION_CHOICE) {
    const size_t best =
        std::max_element(probs.begin(), probs.end()) - probs.begin();
    answer.choice = question.option_names[best];
    answer.confidence = decision_confidence_choice(probs);
  } else {
    double expected = 0.0;
    for (size_t i = 0; i < n; i++) {
      expected += i * probs[i];
    }
    answer.score = expected;
    answer.confidence = decision_confidence_score(probs);
  }
  return answer;
}

//
// answers of a request kept between calls
//

// The loaded model, as far as its answers are concerned: the answers kept
// between calls are only valid for the model that computed them.
std::string model_identity(const llama_model *model) {
  return prompt_cache_model_id() + "|" +
         decision_meta_str(model, "general.name") + "|" +
         std::to_string(llama_model_n_params(model));
}

// FNV-1a over length-prefixed fields, as hex
struct fnv1a {
  uint64_t h = 0xcbf29ce484222325ULL;
  void add(const std::string &s) {
    const std::string field = std::to_string(s.size()) + ":" + s;
    for (const char c : field) {
      h = (h ^ (uint8_t)c) * 0x100000001b3ULL;
    }
  }
  std::string hex() const {
    std::ostringstream oss;
    oss << std::hex << h;
    return oss.str();
  }
};

// FNV-1a over the model and the request, so a re-sent request finds its
// earlier answers, and only when the same model is still loaded
std::string request_hash(const std::string &model_id,
                         const decision_request &req) {
  fnv1a f;
  auto add = [&f](const std::string &s) { f.add(s); };
  add(model_id);
  add(req.state_label);
  add(req.state_label == "Json" ? req.state_json : req.state_text);
  for (size_t i = 0; i < req.q_ids.size(); i++) {
    add(req.q_ids[i]);
    add(req.q_kinds[i]);
    add(req.q_instructions[i]);
  }
  for (size_t j = 0; j < req.o_keys.size(); j++) {
    add(req.o_question_ids[j]);
    add(req.o_keys[j]);
    add(req.o_descriptions[j]);
  }
  return f.hex();
}

// FNV-1a over the model, the context's session layout and the state tokens:
// the name of a stored kev state. The file also holds the tokens, so a
// collision is detected when it is loaded.
std::string state_hash(const std::string &model_id,
                       const std::vector<llama_token> &state) {
  fnv1a f;
  f.add(model_id);
  f.add(prompt_cache_layout_id());
  std::string ids;
  for (const llama_token t : state) {
    ids += std::to_string(t) + ",";
  }
  f.add(ids);
  return f.hex();
}

common_json answer_to_json(const decision_answer &a) {
  common_json j = common_json::object();
  j["id"] = common_json(a.id);
  j["type"] = common_json((int)a.type);
  j["choice"] = common_json(a.choice);
  j["score"] = common_json(a.score);
  j["yes"] = common_json(a.yes);
  j["confidence"] = common_json(a.confidence);
  common_json probs = common_json::array();
  for (const auto &[key, p] : a.probabilities) {
    common_json kp = common_json::object();
    kp["key"] = common_json(key);
    kp["p"] = common_json(p);
    probs.push_back(kp);
  }
  j["probabilities"] = probs;
  return j;
}

// The file is written by this canister only; still, every value is
// type-checked because a get<T>() mismatch would throw (= trap).
bool answer_from_json(const common_json &j, decision_answer &a) {
  if (!j.is_object() || !j.contains("id") || !j.at("id").is_string() ||
      !j.contains("type") || !j.at("type").is_number_integer() ||
      !j.contains("choice") || !j.at("choice").is_string() ||
      !j.contains("probabilities") || !j.at("probabilities").is_array()) {
    return false;
  }
  for (const char *k : {"score", "yes", "confidence"}) {
    if (!j.contains(k) || !j.at(k).is_number()) {
      return false;
    }
  }
  const int type = j.at("type").get<int>();
  if (type < DECISION_QUESTION_CHOICE || type > DECISION_QUESTION_NOUL) {
    return false;
  }
  a.id = j.at("id").get<std::string>();
  a.type = (decision_question_type)type;
  a.choice = j.at("choice").get<std::string>();
  a.score = j.at("score").get<double>();
  a.yes = j.at("yes").get<double>();
  a.confidence = j.at("confidence").get<double>();
  const common_json &probs = j.at("probabilities");
  for (auto it = probs.begin(); it != probs.end(); ++it) {
    const common_json &kp = it.value();
    if (!kp.is_object() || !kp.contains("key") || !kp.at("key").is_string() ||
        !kp.contains("p") || !kp.at("p").is_number()) {
      return false;
    }
    a.probabilities.push_back(
        {kp.at("key").get<std::string>(), kp.at("p").get<double>()});
  }
  return true;
}

// Answers stored by earlier calls of the same request; empty if none (or if
// the file is unreadable, which only costs recomputing them).
std::map<std::string, decision_answer> load_answers(const std::string &path) {
  std::map<std::string, decision_answer> answers;
  std::ifstream in(path);
  if (!in) {
    return answers;
  }
  std::stringstream ss;
  ss << in.rdbuf();
  const common_json j = common_json::parse_no_throw(ss.str());
  if (j.is_discarded() || !j.is_array()) {
    return answers;
  }
  for (auto it = j.begin(); it != j.end(); ++it) {
    decision_answer a;
    if (!answer_from_json(it.value(), a)) {
      return {};
    }
    answers[a.id] = a;
  }
  return answers;
}

bool save_answers(const std::string &path,
                  const std::map<std::string, decision_answer> &answers) {
  common_json j = common_json::array();
  for (const auto &[id, a] : answers) {
    j.push_back(answer_to_json(a));
  }
  std::ofstream out(path, std::ios::trunc);
  if (!out) {
    return false;
  }
  out << j.dump();
  return bool(out);
}

// One readable log line per answered question (shown by icp canister logs)
std::string describe(const decision_answer &a, uint64_t n_tokens) {
  std::ostringstream oss;
  oss.precision(2);
  oss << std::fixed << a.id << " (" << decision_question_type_name(a.type)
      << ", " << n_tokens << " tokens) -> ";
  if (a.type == DECISION_QUESTION_NOUL) {
    oss << (a.yes >= 0.5 ? "yes" : "no") << " (p(yes)=" << a.yes << ")";
  } else {
    const auto best = std::max_element(
        a.probabilities.begin(), a.probabilities.end(),
        [](const auto &x, const auto &y) { return x.second < y.second; });
    if (a.type == DECISION_QUESTION_SCORE) {
      oss << "score " << a.score << ", ";
    }
    oss << best->first << " (p=" << best->second
        << ", confidence=" << a.confidence << ")";
  }
  return oss.str();
}

//
// wire
//

void send_error(IC_API &ic_api, const std::string &msg) {
  ic_api.to_wire(CandidTypeVariant{
      "Err", CandidTypeVariant{"Other", CandidTypeText{msg}}});
}

// The DecisionKind labels, in .did order.
CandidTypeVariant kind_template() {
  CandidTypeVariant v;
  v.append("choice", CandidTypeNull{});
  v.append("score", CandidTypeNull{});
  v.append("noul", CandidTypeNull{});
  return v;
}

// kev: how much of the state is stored
struct kev_progress {
  std::optional<uint64_t> n_state;
  std::optional<uint64_t> n_remaining;
};

void send_result(IC_API &ic_api,
                 const std::vector<decision_question> &questions,
                 const std::map<std::string, decision_answer> &answers,
                 uint64_t input_tokens, const std::vector<std::string> &pending,
                 const kev_progress &progress) {
  std::vector<std::string> a_ids, a_kinds, a_choice;
  std::vector<double> a_score, a_yes, a_confidence;
  std::vector<std::string> p_question_ids, p_keys;
  std::vector<double> p_probability;
  for (const auto &q : questions) { // request order
    const auto it = answers.find(q.id);
    if (it == answers.end()) {
      continue;
    }
    const decision_answer &a = it->second;
    a_ids.push_back(a.id);
    a_kinds.push_back(decision_question_type_name(a.type));
    a_choice.push_back(a.choice);
    a_score.push_back(a.score);
    a_yes.push_back(a.yes);
    a_confidence.push_back(a.confidence);
    for (const auto &[key, p] : a.probabilities) {
      p_question_ids.push_back(a.id);
      p_keys.push_back(key);
      p_probability.push_back(p);
    }
  }

  CandidTypeRecord r_answers;
  r_answers.append("id", CandidTypeVecText{a_ids});
  r_answers.append("kind", CandidTypeVecVariant{kind_template(), a_kinds});
  r_answers.append("choice", CandidTypeVecText{a_choice});
  r_answers.append("score", CandidTypeVecFloat64{a_score});
  r_answers.append("yes", CandidTypeVecFloat64{a_yes});
  r_answers.append("confidence", CandidTypeVecFloat64{a_confidence});

  CandidTypeRecord r_probabilities;
  r_probabilities.append("question_id", CandidTypeVecText{p_question_ids});
  r_probabilities.append("key", CandidTypeVecText{p_keys});
  r_probabilities.append("probability", CandidTypeVecFloat64{p_probability});

  CandidTypeRecord r_out;
  r_out.append("answers", CandidTypeVecRecord{r_answers});
  r_out.append("probabilities", CandidTypeVecRecord{r_probabilities});
  r_out.append("input_tokens", CandidTypeNat64{input_tokens});
  r_out.append("pending", CandidTypeVecText{pending});
  r_out.append("state_tokens", CandidTypeOptNat64{progress.n_state});
  r_out.append("state_tokens_remaining",
               CandidTypeOptNat64{progress.n_remaining});
  ic_api.to_wire(CandidTypeVariant{"Ok", r_out});
}

// A pending question, ready to decode. laya: the whole prompt, pointer -1;
// kev: the question suffix after the state prefix, markers and pointer
// relative to it.
struct task {
  const decision_question *q;
  std::vector<llama_token> tokens;
  std::vector<int32_t> markers;
  int32_t pointer = -1;
};

// The state prefix is decoded without the embeddings output (no outputs
// at all). The context persists across calls, so the flag is restored on
// every exit.
struct embeddings_off {
  llama_context *ctx;
  explicit embeddings_off(llama_context *c) : ctx(c) {
    llama_set_embeddings(ctx, false);
  }
  ~embeddings_off() { llama_set_embeddings(ctx, true); }
};

std::string instructions_note(uint64_t i0, uint64_t i1, uint64_t n_tokens) {
  if (i1 <= i0) {
    return "";
  }
  std::string note = ", " + billions(i1 - i0) + " instructions";
  if (n_tokens > 0) {
    note += " (" + billions((i1 - i0) / n_tokens) + " per token)";
  }
  return note;
}

// The scores of a forward pass and the softmax temperature, for the log
std::string scores_note(const decision_context &d, const decision_question &q,
                        const std::vector<float> &scores) {
  std::ostringstream oss;
  oss.precision(2);
  oss << std::fixed << ", scores [";
  for (size_t k = 0; k < scores.size(); k++) {
    oss << (k ? ", " : "") << scores[k];
  }
  oss << "], softmax T=" << get_temperature(d, q);
  return oss.str();
}

// laya: one forward pass over the whole prompt per question. Returns an error
// message.
std::string answer_laya(llama_context *ctx, const decision_context &d,
                        const std::vector<task> &tasks, uint64_t budget,
                        std::map<std::string, decision_answer> &answers,
                        std::vector<std::string> &pending,
                        uint64_t &input_tokens) {
  for (const auto &t : tasks) {
    const uint64_t n = t.tokens.size();
    if (!pending.empty() || (budget > 0 && input_tokens + n > budget)) {
      pending.push_back(t.q->id);
      continue;
    }
    log_line(t.q->id + ": prompt from the model's systemone template, " +
             std::to_string(n) + " tokens, " +
             std::to_string(t.markers.size()) + " option markers");
    std::vector<float> scores;
    const uint64_t i0 = instruction_counter();
    const std::string error =
        decode(ctx, d, t.tokens, 0, t.markers, t.q->type, -1, scores);
    const uint64_t i1 = instruction_counter();
    if (!error.empty()) {
      return "questions." + t.q->id + ": " + error;
    }
    log_line(t.q->id + ": one forward pass" + instructions_note(i0, i1, 0) +
             scores_note(d, *t.q, scores));
    answers[t.q->id] = format_answer(d, *t.q, scores);
    input_tokens += n;
    log_line(describe(answers[t.q->id], n));
  }
  return "";
}

// A stored kev state is ~20 MB. The cache cleanup timer removes it 6 h after
// it was ingested; until then, a principal keeps at most this many.
const size_t MAX_DECISION_STATES = 8;

// Removes the oldest stored states of the principal of `keep` (by mtime),
// never `keep` itself, so at most MAX_DECISION_STATES remain.
void evict_old_states(const std::string &keep) {
  namespace fs = std::filesystem;
  std::error_code ec;
  std::vector<std::pair<fs::file_time_type, fs::path>> states;
  for (fs::directory_iterator it(fs::path(keep).parent_path(), ec), end;
       !ec && it != end; it.increment(ec)) {
    const std::string name = it->path().filename().string();
    if (string_starts_with(name, "decision-state-") &&
        string_ends_with(name, ".session") && it->path() != fs::path(keep)) {
      std::error_code ec_time;
      const auto mtime = fs::last_write_time(it->path(), ec_time);
      if (!ec_time) {
        states.emplace_back(mtime, it->path());
      }
    }
  }
  if (states.size() < MAX_DECISION_STATES) {
    return;
  }
  std::sort(states.begin(), states.end());
  for (size_t i = 0; i + MAX_DECISION_STATES <= states.size(); i++) {
    const std::string old = states[i].second.string();
    std::error_code ec_remove;
    fs::remove(old, ec_remove);
    prompt_cache_remove_stamp(old);
    log_line("state: removed the older stored state " + old);
  }
}

// The IC ends an update call at 40 B instructions, and a trap loses all of its
// work: a state that does not fit would then be retried forever. answer_kev
// stops before it would pass this, and leaves the rest pending. Native builds
// count no instructions, so there is no guard there.
const uint64_t KEV_INSTRUCTIONS_MAX = 36000000000ULL;

// The state is decoded in steps of this many tokens, aligned to multiples of
// it, so the steps are the same however the calls are split (for a token
// budget that is a multiple of it), and a call can stop between any two.
const size_t KEV_STEP = 8;

// Instructions per token grow with the tokens already in the memory, at a rate
// that depends on the attention setup. Measured on the local replica (per
// token at 0 / ~550 / ~2000 tokens), with a margin:
// - -fa off, f32 KV cache: 1.42 / 1.45 / 1.60 B -> 0.15 M more per token
// - -fa off, f16 KV cache: 1.42 / 1.76 B        -> 0.8 M
// - flash attention on:    1.42 / 2.05 B        -> 1.3 M
// A call that measures more uses that instead. The estimate must not rely on
// what earlier calls learned: a trap rolls that back, and would repeat.
double kev_rate_estimate(size_t n_kv) {
  const std::string layout = prompt_cache_layout_id();
  const double slope = layout.find("fa=1") != std::string::npos     ? 1.3e6
                       : layout.find("tk=f32") != std::string::npos ? 1.5e5
                                                                    : 8e5;
  return 1.45e9 + slope * n_kv;
}

// Saving / loading the stored state, per byte of it (measured: 0.05-0.08 to
// save, ~0.03 to load), with a margin
const double KEV_SAVE_PER_BYTE = 0.1;
const double KEV_LOAD_PER_BYTE = 0.05;

struct kev_guard {
  const bool on = instruction_counter() > 0;
  double rate = 0.0; // the highest instructions per token measured this call

  void measured(uint64_t i0, uint64_t i1, size_t n_tokens) {
    if (on && n_tokens > 0) {
      rate = std::max(rate, double(i1 - i0) / n_tokens);
    }
  }
  double per_token(size_t n_kv) const {
    return std::max(rate, kev_rate_estimate(n_kv));
  }
  // whether `cost` more instructions keep this call within the limit
  bool fits(double cost) const {
    return !on || instruction_counter() + cost <= KEV_INSTRUCTIONS_MAX;
  }
};

// The tokens of a stored state, read from the header of its session file
// (magic, version, token count, tokens) without loading the state itself.
bool read_stored_tokens(const std::string &path, size_t n_max,
                        std::vector<llama_token> &tokens) {
  std::ifstream in(path, std::ios::binary);
  uint32_t magic = 0, version = 0, n_tokens = 0;
  in.read(reinterpret_cast<char *>(&magic), sizeof(magic));
  in.read(reinterpret_cast<char *>(&version), sizeof(version));
  in.read(reinterpret_cast<char *>(&n_tokens), sizeof(n_tokens));
  if (!in || magic != LLAMA_SESSION_MAGIC || version != LLAMA_SESSION_VERSION ||
      n_tokens > n_max) {
    return false;
  }
  tokens.resize(n_tokens);
  in.read(reinterpret_cast<char *>(tokens.data()),
          n_tokens * sizeof(llama_token));
  return bool(in);
}

// A state that grows by appending (a log with a new day, a game's move list)
// starts with a state stored earlier. Returns the principal's stored state
// with the longest token list that is a prefix of `state`, other than `path`
// itself, written by this build for the loaded model and context layout; ""
// if there is none.
std::string find_stored_prefix(const std::string &path,
                               const std::vector<llama_token> &state) {
  namespace fs = std::filesystem;
  std::string best;
  size_t n_best = 0;
  std::error_code ec;
  for (fs::directory_iterator it(fs::path(path).parent_path(), ec), end;
       !ec && it != end; it.increment(ec)) {
    const std::string candidate = it->path().string();
    const std::string name = it->path().filename().string();
    if (!string_starts_with(name, "decision-state-") ||
        !string_ends_with(name, ".session") || it->path() == fs::path(path) ||
        !prompt_cache_format_is_current(candidate)) {
      continue;
    }
    std::vector<llama_token> tokens;
    if (read_stored_tokens(candidate, state.size(), tokens) &&
        tokens.size() > n_best &&
        std::equal(tokens.begin(), tokens.end(), state.begin())) {
      best = candidate;
      n_best = tokens.size();
    }
  }
  return best;
}

// kev: ingest the state prefix into a per-principal session file across calls
// (the checkpoint), then answer questions from that checkpoint. The file is
// only written while the state is ingested: a question changes the memory, and
// a recurrent memory cannot be rolled back, so every question after the first
// one of a call re-loads the checkpoint. Returns an error message.
std::string answer_kev(llama_context *ctx, const decision_context &d,
                       const std::string &principal_id,
                       const std::string &model_id,
                       const std::vector<llama_token> &state,
                       const std::vector<task> &tasks, uint64_t budget,
                       std::map<std::string, decision_answer> &answers,
                       std::vector<std::string> &pending,
                       uint64_t &input_tokens, kev_progress &progress) {
  const size_t n_state = state.size();
  std::string path, path_error;
  if (!get_canister_path_session("decision-state-" +
                                     state_hash(model_id, state) + ".session",
                                 principal_id, path, path_error)) {
    return path_error;
  }
  llama_memory_t mem = llama_get_memory(ctx);
  kev_guard guard;

  // --- the checkpoint so far. The in-heap memory is never trusted: another
  //     call (or principal) may have used the context since.
  std::string stale_msg;
  if (prompt_cache_discard_if_stale(path, stale_msg)) {
    log_line(stale_msg);
  }
  llama_memory_clear(mem, true);
  // Loads a stored state into the memory; true if it holds the start of this
  // state (then n_cached is its length)
  size_t n_cached = 0;
  auto load = [&](const std::string &file) {
    std::vector<llama_token> stored(llama_n_ctx(ctx));
    size_t n_stored = 0;
    const uint64_t i0 = instruction_counter();
    const bool ok =
        llama_state_load_file(ctx, file.c_str(), stored.data(), stored.size(),
                              &n_stored) &&
        n_stored <= n_state &&
        std::equal(stored.begin(), stored.begin() + n_stored, state.begin());
    const uint64_t i1 = instruction_counter();
    if (!ok) {
      llama_memory_clear(mem, true);
      return false;
    }
    n_cached = n_stored;
    log_line("state: loaded " + std::to_string(n_cached) + "/" +
             std::to_string(n_state) + " tokens from " + file +
             instructions_note(i0, i1, 0));
    return true;
  };
  std::string loaded; // the file the memory was loaded from, if any
  std::error_code ec_size;
  if (std::filesystem::file_size(path, ec_size) > 0 && !ec_size) {
    if (load(path)) {
      loaded = path;
    } else {
      std::error_code ec;
      std::filesystem::remove(path, ec);
      prompt_cache_remove_stamp(path);
      log_line("state: discarded " + path +
               ", it does not hold this state; starting over");
    }
  }
  const bool new_file = loaded.empty();
  if (new_file) {
    // a grown state continues from the stored state it starts with
    const std::string prefix = find_stored_prefix(path, state);
    if (!prefix.empty() && load(prefix)) {
      loaded = prefix;
      log_line("state: continues the stored state " + prefix);
    }
  }

  // --- ingest the rest of the state, in steps of KEV_STEP tokens, while the
  //     token budget and the instruction limit allow (the save included)
  std::error_code ec_bytes;
  const uintmax_t stored_bytes =
      loaded.empty() ? 0 : std::filesystem::file_size(loaded, ec_bytes);
  const double save_cost =
      KEV_SAVE_PER_BYTE *
      std::max<double>(ec_bytes ? 0 : stored_bytes, 25.0 * 1024 * 1024);
  uint64_t used = 0;
  if (n_cached < n_state) {
    if (new_file) {
      evict_old_states(path);
    }
    const size_t n_start = n_cached;
    {
      embeddings_off no_embd(ctx);
      while (n_cached < n_state && (budget == 0 || used < budget)) {
        size_t end = std::min(n_state, (n_cached / KEV_STEP + 1) * KEV_STEP);
        if (budget > 0) {
          end = std::min(end, n_cached + (size_t)(budget - used));
        }
        if (!guard.fits((end - n_cached) * guard.per_token(end) + save_cost)) {
          break;
        }
        const uint64_t i0 = instruction_counter();
        const std::string error = decode_prefix(ctx, state, n_cached, end);
        const uint64_t i1 = instruction_counter();
        if (!error.empty()) {
          return error;
        }
        log_line("state: tokens " + std::to_string(n_cached) + ".." +
                 std::to_string(end) + " of " + std::to_string(n_state) +
                 instructions_note(i0, i1, end - n_cached));
        guard.measured(i0, i1, end - n_cached);
        used += end - n_cached;
        n_cached = end;
      }
    }
    if (n_cached == n_start) {
      return "the stored state is too large to add to it within one update "
             "call (" +
             std::to_string(n_cached) + " of " + std::to_string(n_state) +
             " tokens stored): use a smaller state or a smaller --ctx-size";
    }
    const uint64_t i0 = instruction_counter();
    if (!llama_state_save_file(ctx, path.c_str(), state.data(), n_cached)) {
      return "failed to save the state to " + path;
    }
    prompt_cache_write_format_stamp(path);
    const uint64_t i1 = instruction_counter();
    std::error_code ec;
    log_line("state: saved " + std::to_string(n_cached) + " tokens, " +
             std::to_string(std::filesystem::file_size(path, ec)) + " bytes" +
             instructions_note(i0, i1, 0));
  }
  progress.n_state = n_state;
  progress.n_remaining = n_state - n_cached;
  input_tokens = used;
  if (n_cached < n_state) {
    for (const auto &t : tasks) {
      pending.push_back(t.q->id);
    }
    return "";
  }

  // --- answer questions from the checkpoint while they fit the budget and
  //     the instruction limit (a question is a few % dearer per token: every
  //     token is an output)
  std::error_code ec_file;
  const double reload_cost =
      KEV_LOAD_PER_BYTE * std::filesystem::file_size(path, ec_file);
  bool at_checkpoint = true;
  for (const auto &t : tasks) {
    const uint64_t n = t.tokens.size();
    const double cost = (at_checkpoint ? 0.0 : reload_cost) +
                        1.05 * n * guard.per_token(n_state + n);
    if (pending.empty() && used == 0 && !guard.fits(cost)) {
      return "questions." + t.q->id + ": " + std::to_string(n) +
             " tokens on a stored state of " + std::to_string(n_state) +
             " tokens need more instructions than one update call allows: "
             "use a shorter question or a smaller state";
    }
    if (!pending.empty() || (budget > 0 && used + n > budget) ||
        !guard.fits(cost)) {
      pending.push_back(t.q->id);
      continue;
    }
    if (!at_checkpoint) {
      const uint64_t i0 = instruction_counter();
      llama_memory_clear(mem, true);
      std::vector<llama_token> stored(n_state);
      size_t n_stored = 0;
      if (!llama_state_load_file(ctx, path.c_str(), stored.data(),
                                 stored.size(), &n_stored) ||
          n_stored != n_state) {
        return "failed to re-load the stored state from " + path;
      }
      const uint64_t i1 = instruction_counter();
      log_line("state: re-loaded the checkpoint" +
               instructions_note(i0, i1, 0));
    }
    std::vector<float> scores;
    const uint64_t i0 = instruction_counter();
    const std::string error =
        decode(ctx, d, t.tokens, n_state, t.markers, 0, t.pointer, scores);
    const uint64_t i1 = instruction_counter();
    if (!error.empty()) {
      return "questions." + t.q->id + ": " + error;
    }
    at_checkpoint = false;
    guard.measured(i0, i1, n);
    log_line(t.q->id + ": " + std::to_string(n) + " tokens after the state" +
             instructions_note(i0, i1, n) + scores_note(d, *t.q, scores));
    answers[t.q->id] = format_answer(d, *t.q, scores);
    used += n;
    log_line(describe(answers[t.q->id], n));
  }
  input_tokens = used;
  return "";
}

} // namespace

void decision_init(const llama_model *model) { init(g_decision, model); }

void decision_reset() { g_decision = decision_context(); }

bool decision_model_loaded() {
  return g_decision.type != COMMON_DECISION_TYPE_NONE;
}

void run_decision() {
  IC_API ic_api(CanisterUpdate{std::string(__func__)}, false);
  if (!has_admin_update_or_whitelisted(ic_api)) {
    send_access_denied_api_error(ic_api);
    return;
  }
  const std::string principal_id = ic_api.get_caller().get_text();

  // --- get the data from the wire
  decision_request req;
  CandidTypeVariant state{&req.state_label};
  state.append("Text", CandidTypeText{&req.state_text});
  state.append("Json", CandidTypeText{&req.state_json});

  CandidTypeVariant kind_template;
  kind_template.append("choice", CandidTypeNull{});
  kind_template.append("score", CandidTypeNull{});
  kind_template.append("noul", CandidTypeNull{});

  CandidTypeRecord r_questions;
  r_questions.append("id", CandidTypeVecText{&req.q_ids});
  r_questions.append("kind",
                     CandidTypeVecVariant{&kind_template, &req.q_kinds});
  r_questions.append("instructions", CandidTypeVecText{&req.q_instructions});

  CandidTypeRecord r_options;
  r_options.append("question_id", CandidTypeVecText{&req.o_question_ids});
  r_options.append("key", CandidTypeVecText{&req.o_keys});
  r_options.append("description", CandidTypeVecText{&req.o_descriptions});

  CandidTypeRecord r_in;
  r_in.append("state", state);
  r_in.append("questions", CandidTypeVecRecord{&r_questions});
  r_in.append("options", CandidTypeVecRecord{&r_options});
  ic_api.from_wire(r_in);

  // --- the loaded model
  llama_context *ctx = icpp_persisted_ctx();
  if (ctx == nullptr) {
    send_error(ic_api, "no model loaded");
    return;
  }
  const decision_context &d = g_decision;
  if (d.type == COMMON_DECISION_TYPE_NONE) {
    send_error(ic_api, "the loaded model is not a decision model");
    return;
  }
  if (!d.error.empty()) {
    send_error(ic_api, d.error);
    return;
  }

  common_json state_json;
  std::vector<decision_question> questions;
  const std::string error = parse(d, req, state_json, questions);
  if (!error.empty()) {
    send_error(ic_api, error);
    return;
  }

  // --- answers of earlier calls of this request
  std::string path, path_error;
  const std::string model_id = model_identity(llama_get_model(ctx));
  if (!get_canister_path_session("decision-" + request_hash(model_id, req) +
                                     ".json",
                                 principal_id, path, path_error)) {
    send_error(ic_api, path_error);
    return;
  }
  std::map<std::string, decision_answer> answers = load_answers(path);
  log_line("request: " + std::to_string(questions.size()) + " question(s), " +
           req.state_label + " state of " +
           std::to_string(req.state_label == "Json" ? req.state_json.size()
                                                    : req.state_text.size()) +
           " bytes, token budget " +
           (max_tokens_update ? std::to_string(max_tokens_update) : "none") +
           (answers.empty() ? ""
                            : ", " + std::to_string(answers.size()) +
                                  " answered by earlier calls"));

  // --- prompts of the pending questions. All are checked BEFORE any forward
  //     pass (rendering + tokenizing costs ~1/1000 of a pass), so an invalid
  //     question never discards the work of this call.
  const uint64_t budget = max_tokens_update; // 0 = no limit
  const bool kev = d.type == COMMON_DECISION_TYPE_KEV;
  // kev: the state prefix, decoded over calls; laya: the whole prompt
  const uint64_t n_tokens_max =
      kev ? llama_n_ubatch(ctx)
          : std::min(llama_n_ubatch(ctx), llama_n_ctx(ctx));
  if (kev && (llama_n_seq_max(ctx) != 1 || llama_n_rs_seq(ctx) != 0)) {
    send_error(ic_api, "a kev decision model needs one sequence: load it "
                       "without --parallel / --kv-unified options");
    return;
  }
  std::vector<llama_token> state_tokens; // kev
  std::vector<task> tasks;
  for (const auto &q : questions) {
    if (answers.count(q.id)) {
      continue;
    }
    task t{&q,
           common_tokenize(d.vocab, render(d, state_json, q), false, true),
           {}};
    std::string layout_error;
    if (kev) {
      size_t state_end = 0;
      layout_error =
          fill_task_kev(d, t.tokens, q, t.markers, t.pointer, state_end);
      if (layout_error.empty()) {
        // the state prefix is the same for every question of the request
        const std::vector<llama_token> prefix(t.tokens.begin(),
                                              t.tokens.begin() + state_end);
        if (state_tokens.empty()) {
          state_tokens = prefix;
        } else if (prefix != state_tokens) {
          layout_error = "unexpected layout of the decision prompt";
        }
        t.tokens.erase(t.tokens.begin(), t.tokens.begin() + state_end);
        for (auto &m : t.markers) {
          m -= state_end;
        }
        t.pointer -= state_end;
      }
    } else {
      layout_error = fill_task_laya(d, t.tokens, q, t.markers);
    }
    if (!layout_error.empty()) {
      send_error(ic_api, "questions." + q.id + ": " + layout_error);
      return;
    }
    const uint64_t n = t.tokens.size();
    if (kev && state_tokens.size() + n > llama_n_ctx(ctx)) {
      send_error(ic_api, "questions." + q.id + ": the state (" +
                             std::to_string(state_tokens.size()) +
                             " tokens) and the question (" + std::to_string(n) +
                             " tokens) are more than the loaded --ctx-size (" +
                             std::to_string(llama_n_ctx(ctx)) + ")");
      return;
    }
    if (n > n_tokens_max) {
      send_error(ic_api, "questions." + q.id + ": the prompt has " +
                             std::to_string(n) +
                             " tokens, more than the loaded --ubatch-size / "
                             "--ctx-size allow (" +
                             std::to_string(n_tokens_max) + ")");
      return;
    }
    if (budget > 0 && n > budget) {
      send_error(ic_api, "questions." + q.id + ": the prompt has " +
                             std::to_string(n) +
                             " tokens, more than one update call can evaluate "
                             "(max_tokens_update = " +
                             std::to_string(budget) + ")");
      return;
    }
    tasks.push_back(std::move(t));
  }

  // --- answer the pending questions, in request order, while they fit this
  //     call's token budget; the rest is answered by the next call
  uint64_t input_tokens = 0;
  std::vector<std::string> pending;
  kev_progress progress;
  const std::string answer_error =
      !kev ? answer_laya(ctx, d, tasks, budget, answers, pending, input_tokens)
      : tasks.empty()
          ? ""
          : answer_kev(ctx, d, principal_id, model_id, state_tokens, tasks,
                       budget, answers, pending, input_tokens, progress);
  if (!answer_error.empty()) {
    send_error(ic_api, answer_error);
    return;
  }

  // --- keep the answers for the next call, or clean up when complete
  if (pending.empty()) {
    std::error_code ec;
    std::filesystem::remove(path, ec);
  } else if (!save_answers(path, answers)) {
    send_error(ic_api, "failed to save the answers so far to " + path);
    return;
  }

  {
    std::string summary = std::to_string(input_tokens) + " tokens this call, " +
                          std::to_string(answers.size()) + "/" +
                          std::to_string(questions.size()) +
                          " questions answered";
    for (size_t i = 0; i < pending.size(); i++) {
      summary += (i ? ", " : ", pending: ") + pending[i];
    }
    log_line(summary);
  }

  send_result(ic_api, questions, answers, input_tokens, pending, progress);
}
