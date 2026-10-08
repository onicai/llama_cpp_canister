// Typed decision models: run_decision.
//
// A port of the laya path of llama.cpp tools/server/server-decision.cpp (b11476),
// keeping its function names (init, parse, render, render_options, fill_task_laya,
// get_temperature, format_answer) so it can be diffed on each llama.cpp upgrade.
// Left out: images, the other decision types (they need models too large for a
// canister), and shared-prefix grouping.
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
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
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
};

decision_context g_decision;

std::string decision_meta_str(const llama_model *model,
                              const std::string &key) {
  char buf[256];
  const int32_t n =
      llama_model_meta_val_str(model, key.c_str(), buf, sizeof(buf));
  return n < 0 ? "" : std::string(buf);
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

  if (model_type != COMMON_DECISION_TYPE_LAYA) {
    d.error = "decision model type '" + type_name +
              "' is not supported by llama_cpp_canister (supported: laya, "
              "e.g. Julia-1 and Laya)";
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

  d.token_marker = llama_vocab_mask(d.vocab);
  d.token_sep = llama_vocab_sep(d.vocab);
  if (d.token_marker == LLAMA_TOKEN_NULL || d.token_sep == LLAMA_TOKEN_NULL) {
    d.error = "decision model has no mask or sep token";
    return;
  }
  d.text_marker = common_token_to_piece(d.vocab, d.token_marker, true);

  d.max_head_tokens =
      std::strtoul(decision_meta_str(model, prefix + "max_head_tokens").c_str(),
                   nullptr, 10);
  if (d.max_head_tokens == 0) {
    d.error = "decision model has no valid max_head_tokens";
    return;
  }
  d.n_options_max = 255;

  std::cout << "llama_cpp: decision_init - decision model '"
            << decision_meta_str(model, "general.name") << "', type "
            << type_name << ", systemone template, " << d.temperatures.size()
            << " softmax temperatures, max_head_tokens " << d.max_head_tokens
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

common_json render_options(const decision_question &question) {
  common_json options = common_json::array();
  for (const auto &opt : question.options) {
    common_json option = common_json::object();
    option["key"] = common_json(opt.key);
    option["description"] = opt.description.empty()
                                ? common_json(nullptr)
                                : common_json(opt.description);
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
  inp["options"] = render_options(question);

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

// One forward pass; the score of each option is the embeddings output at its
// marker, in the column of the question type. Returns an error message.
std::string decode(llama_context *ctx, const std::vector<llama_token> &tokens,
                   const std::vector<int32_t> &markers, int32_t column,
                   std::vector<float> &scores) {
  llama_batch batch = llama_batch_init(tokens.size(), 0, 1);
  for (size_t i = 0; i < tokens.size(); i++) {
    batch.token[i] = tokens[i];
    batch.pos[i] = i;
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
  for (const int32_t marker : markers) {
    const float *embd = llama_get_embeddings_ith(ctx, marker);
    if (embd == nullptr) {
      return "failed to get the embeddings of an option";
    }
    scores.push_back(embd[column]);
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

// FNV-1a over the model and the request, so a re-sent request finds its
// earlier answers, and only when the same model is still loaded
std::string request_hash(const std::string &model_id,
                         const decision_request &req) {
  uint64_t h = 0xcbf29ce484222325ULL;
  auto add = [&h](const std::string &s) {
    const std::string field = std::to_string(s.size()) + ":" + s;
    for (const char c : field) {
      h = (h ^ (uint8_t)c) * 0x100000001b3ULL;
    }
  };
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
  std::ostringstream oss;
  oss << std::hex << h;
  return oss.str();
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

void send_result(IC_API &ic_api,
                 const std::vector<decision_question> &questions,
                 const std::map<std::string, decision_answer> &answers,
                 uint64_t input_tokens,
                 const std::vector<std::string> &pending) {
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
  ic_api.to_wire(CandidTypeVariant{"Ok", r_out});
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
  const uint64_t n_tokens_max = std::min(llama_n_ubatch(ctx), llama_n_ctx(ctx));
  struct task {
    const decision_question *q;
    std::vector<llama_token> tokens;
    std::vector<int32_t> markers;
  };
  std::vector<task> tasks;
  for (const auto &q : questions) {
    if (answers.count(q.id)) {
      continue;
    }
    task t{&q,
           common_tokenize(d.vocab, render(d, state_json, q), false, true),
           {}};
    const std::string layout_error = fill_task_laya(d, t.tokens, q, t.markers);
    if (!layout_error.empty()) {
      send_error(ic_api, "questions." + q.id + ": " + layout_error);
      return;
    }
    const uint64_t n = t.tokens.size();
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
    const std::string decode_error =
        decode(ctx, t.tokens, t.markers, t.q->type, scores);
    const uint64_t i1 = instruction_counter();
    if (!decode_error.empty()) {
      send_error(ic_api, "questions." + t.q->id + ": " + decode_error);
      return;
    }
    {
      std::ostringstream oss;
      oss.precision(2);
      oss << std::fixed << t.q->id << ": one forward pass"
          << (i1 > i0 ? ", " + billions(i1 - i0) + " instructions" : "")
          << ", scores [";
      for (size_t k = 0; k < scores.size(); k++) {
        oss << (k ? ", " : "") << scores[k];
      }
      oss << "], softmax T=" << get_temperature(d, *t.q);
      log_line(oss.str());
    }
    answers[t.q->id] = format_answer(d, *t.q, scores);
    input_tokens += n;
    log_line(describe(answers[t.q->id], n));
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

  send_result(ic_api, questions, answers, input_tokens, pending);
}
