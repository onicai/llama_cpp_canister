// Sequence classifiers: run_classify and run_classify_query.
//
// What llama-embedding does for `--pooling rank --embd-normalize -1`
// (examples/embedding/embedding.cpp, batch_decode): one forward pass over the
// whole text, then the n_cls_out raw scores of the classification head from
// llama_get_embeddings_seq. The model is loaded by load_model with
// --embedding --pooling rank, so the context already does the pooling.
//
// A classifier is a bidirectional encoder: the whole text is decoded in one
// ubatch, so one call cannot be split over several messages. The token count
// is checked against the context before the decode; llama.cpp asserts on it
// (a trap) instead of returning an error.

#include "classify.h"

#include "auth.h"
#include "ic_api.h"
#include "main_.h"
#include "utils.h"

// "common/" prefix: a bare "common.h" resolves to ggml/src/ggml-cpu/common.h
#include "common/common.h"
#include "llama.h"

#include <algorithm>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

bool classify_model_loaded() {
  llama_context *ctx = icpp_persisted_ctx();
  // n_cls_out alone is not enough: llama.cpp defaults it to 1 for every model.
  // A classifier converted from ...ForSequenceClassification has its labels.
  return ctx != nullptr && llama_pooling_type(ctx) == LLAMA_POOLING_TYPE_RANK &&
         llama_model_cls_label(llama_get_model(ctx), 0) != nullptr;
}

namespace {

struct classify_result {
  std::string error;
  std::vector<double> scores;
  std::vector<std::string> labels;
  uint64_t input_tokens = 0;
};

void log_line(const std::string &msg) {
  std::cout << "llama_cpp: run_classify - " << msg << std::endl;
}

// One forward pass over the text.
classify_result classify_one(const std::string &text) {
  classify_result r;
  llama_context *ctx = icpp_persisted_ctx();
  if (ctx == nullptr) {
    r.error = "no model loaded";
    return r;
  }
  if (!classify_model_loaded()) {
    r.error = "the loaded model is not a sequence classifier: load it with "
              "--embedding --pooling rank";
    return r;
  }
  if (text.empty()) {
    r.error = "text is empty";
    return r;
  }
  if (text.size() > CLASSIFY_MAX_TEXT_BYTES) {
    r.error = "text is " + std::to_string(text.size()) +
              " bytes, more than the limit of " +
              std::to_string(CLASSIFY_MAX_TEXT_BYTES);
    return r;
  }

  const llama_model *model = llama_get_model(ctx);
  const llama_vocab *vocab = llama_model_get_vocab(model);
  // add_special: the model's BOS/CLS and EOS/SEP, as llama-embedding adds
  // them. parse_special off: the text cannot inject special tokens.
  const std::vector<llama_token> tokens =
      common_tokenize(vocab, text, true, false);
  const uint32_t n_max =
      std::min({llama_n_ubatch(ctx), llama_n_batch(ctx), llama_n_ctx(ctx)});
  if (tokens.size() > n_max) {
    r.error = "text is " + std::to_string(tokens.size()) +
              " tokens, more than the " + std::to_string(n_max) +
              " of the loaded context (-c, -b, -ub of load_model)";
    return r;
  }

  llama_memory_clear(llama_get_memory(ctx), true);
  llama_batch batch = llama_batch_init(tokens.size(), 0, 1);
  for (size_t i = 0; i < tokens.size(); i++) {
    batch.token[i] = tokens[i];
    batch.pos[i] = i;
    batch.n_seq_id[i] = 1;
    batch.seq_id[i][0] = 0;
    // every token is an output: llama.cpp requires that for an embeddings
    // context
    batch.logits[i] = true;
  }
  batch.n_tokens = tokens.size();
  const uint64_t i0 = instruction_counter();
  const int32_t rc = llama_decode(ctx, batch);
  const uint64_t i1 = instruction_counter();
  llama_batch_free(batch);
  if (rc != 0) {
    r.error = "llama_decode failed with code " + std::to_string(rc);
    return r;
  }

  const float *scores = llama_get_embeddings_seq(ctx, 0);
  if (scores == nullptr) {
    r.error = "failed to get the classifier scores";
    return r;
  }
  const uint32_t n_cls_out = llama_model_n_cls_out(model);
  for (uint32_t i = 0; i < n_cls_out; i++) {
    r.scores.push_back(scores[i]);
    const char *label = llama_model_cls_label(model, i);
    r.labels.push_back(label != nullptr ? label : std::to_string(i));
  }
  r.input_tokens = tokens.size();
  log_line(std::to_string(tokens.size()) + " tokens, " +
           std::to_string(n_cls_out) + " scores, " + std::to_string(i1 - i0) +
           " instructions");
  return r;
}

std::string classify_request_from_wire(IC_API &ic_api) {
  std::string text;
  CandidTypeRecord r_in;
  r_in.append("text", CandidTypeText{&text});
  ic_api.from_wire(r_in);
  return text;
}

void send_result(IC_API &ic_api, const classify_result &r) {
  if (!r.error.empty()) {
    ic_api.to_wire(CandidTypeVariant{
        "Err", CandidTypeVariant{"Other", CandidTypeText{r.error}}});
    return;
  }
  CandidTypeRecord r_out;
  r_out.append("scores", CandidTypeVecFloat64{r.scores});
  r_out.append("labels", CandidTypeVecText{r.labels});
  r_out.append("input_tokens", CandidTypeNat64{r.input_tokens});
  const uint64_t instructions = call_instruction_counter();
  r_out.append("instructions",
               CandidTypeOptNat64{instructions
                                      ? std::optional<uint64_t>(instructions)
                                      : std::nullopt});
  ic_api.to_wire(CandidTypeVariant{"Ok", r_out});
}

} // namespace

void run_classify() {
  IC_API ic_api(CanisterUpdate{std::string(__func__)}, false);
  if (!has_admin_update_or_whitelisted(ic_api)) {
    send_access_denied_api_error(ic_api);
    return;
  }
  send_result(ic_api, classify_one(classify_request_from_wire(ic_api)));
}

// The same as a query: the IC discards what it changes in the context
void run_classify_query() {
  IC_API ic_api(CanisterQuery{std::string(__func__)}, false);
  if (!has_admin_query_or_whitelisted(ic_api)) {
    send_access_denied_api_error(ic_api);
    return;
  }
  send_result(ic_api, classify_one(classify_request_from_wire(ic_api)));
}
