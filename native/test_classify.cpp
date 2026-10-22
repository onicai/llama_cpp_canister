// Native tests for run_classify and run_classify_query, with
// onicai/dao-moderation-classifier (f32): a ModernBERT sequence classifier with
// 15 labels that helps moderate a DAO's chat channel (README-classifier-models.md).
// The reference scores come from llama.cpp on the CPU:
//   llama-embedding -m <gguf> --pooling rank -p "<text>" --embd-normalize -1
// Skipped when the model is not downloaded.
//
// Floats differ between arm64 and x86_64, so the replies are decoded and the
// scores checked within a tolerance instead of comparing Candid hex.
//
// The input hex was encoded with didc.

#include "test_classify.h"

#include "../src/classify.h"
#include "../src/decision.h"
#include "../src/model.h"
#include "../src/run.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <optional>
#include <string>
#include <tuple>
#include <vector>

namespace {

// A decoded run_classify reply
struct ClassifyReply {
  std::string label;     // "Ok" or "Err"
  std::string err_label; // "Other" or "StatusCode"
  std::string err_text;
  uint16_t err_code{0};
  std::vector<double> scores;
  std::vector<std::string> labels;
  uint64_t input_tokens{0};
  std::optional<uint64_t> instructions; // null in native builds
};

ClassifyReply decode_reply(const std::string &hex) {
  ClassifyReply r;
  CandidTypeRecord ok;
  ok.append("scores", CandidTypeVecFloat64{&r.scores});
  ok.append("labels", CandidTypeVecText{&r.labels});
  ok.append("input_tokens", CandidTypeNat64{&r.input_tokens});
  ok.append("instructions", CandidTypeOptNat64{&r.instructions});

  CandidTypeVariant err{&r.err_label};
  err.append("Other", CandidTypeText{&r.err_text});
  err.append("StatusCode", CandidTypeNat16{&r.err_code});

  CandidTypeVariant result{&r.label};
  result.append("Ok", ok);
  result.append("Err", err);

  CandidArgs args;
  args.append(result);
  CandidDeserialize(hex, args);
  return r;
}

int g_failures = 0;

void check(bool ok, const std::string &what) {
  if (!ok) {
    ++g_failures;
    std::cout << "FAIL - test_classify: " << what << std::endl;
  }
}

ClassifyReply call(MockIC &mockIC, const std::string &name,
                   const std::string &input, const std::string &caller,
                   void (*method)() = run_classify) {
  std::string out;
  mockIC.run_test(std::string("test_classify: ") + name, method, input, "",
                  false, caller, &out);
  return decode_reply(out);
}

void expect_error(MockIC &mockIC, const std::string &name,
                  const std::string &input, const std::string &caller,
                  const std::string &message) {
  const ClassifyReply r = call(mockIC, name, input, caller);
  check(r.label == "Err" && r.err_label == "Other" && r.err_text == message,
        name + ": expected Err '" + message + "', got " + r.label + " '" +
            r.err_text + "'");
}

// The scores of a reference text, within tol of upstream's
void check_scores(const ClassifyReply &r, const std::string &name,
                  const std::vector<double> &reference, double tol) {
  check(r.label == "Ok", name + ": not Ok: " + r.err_text);
  check(r.scores.size() == reference.size(),
        name + ": " + std::to_string(r.scores.size()) + " scores, expected " +
            std::to_string(reference.size()));
  check(r.labels.size() == reference.size(), name + ": number of labels");
  check(r.input_tokens > 2, name + ": input_tokens");
  check(!r.instructions.has_value(), name + ": instructions not null");
  check(std::all_of(r.scores.begin(), r.scores.end(),
                    [](double x) { return std::isfinite(x); }),
        name + ": a score is not finite");
  if (r.scores.size() != reference.size()) {
    return;
  }
  double max_diff = 0.0;
  for (size_t i = 0; i < reference.size(); i++) {
    max_diff = std::max(max_diff, std::fabs(r.scores[i] - reference[i]));
  }
  check(max_diff <= tol, name + ": a score differs " +
                             std::to_string(max_diff) + " from the reference");
  const auto top = [](const std::vector<double> &v) {
    return std::max_element(v.begin(), v.end()) - v.begin();
  };
  check(top(r.scores) == top(reference), name + ": top label differs");
}

} // namespace

void test_classify(MockIC &mockIC) {
  const std::string model =
      "models/onicai/dao-moderation-classifier/dao-moderation-f32.gguf";
  if (!std::filesystem::exists(model)) {
    std::cout << "SKIP - test_classify: " << model << " not found" << std::endl;
    return;
  }
  std::string controller{MOCKIC_CONTROLLER};
  std::string anonymous{"2vxsx-fae"};
  bool silent_on_trap = true;

  const std::string LOAD_CLASSIFIER =
      "4449444c026c01dd9ad28304016d7101000a072d2d6d6f64656c3f6d6f64656c732f6f6e696361692f64616f2d6d6f6465726174696f6e2d636c61737369666965722f64616f2d6d6f6465726174696f6e2d6633322e676775660b2d2d656d62656464696e67092d2d706f6f6c696e670472616e6b0b2d2d6e6f2d7761726d7570022d6303353132032d756203353132";
  const std::string LOAD_CLASSIFIER_CTX_8 =
      "4449444c026c01dd9ad28304016d7101000c072d2d6d6f64656c3f6d6f64656c732f6f6e696361692f64616f2d6d6f6465726174696f6e2d636c61737369666965722f64616f2d6d6f6465726174696f6e2d6633322e676775660b2d2d656d62656464696e67092d2d706f6f6c696e670472616e6b0b2d2d6e6f2d7761726d7570022d630138022d620138032d75620138";
  const std::string LOAD_CLASSIFIER_NO_POOLING =
      "4449444c026c01dd9ad28304016d71010005072d2d6d6f64656c3f6d6f64656c732f6f6e696361692f64616f2d6d6f6465726174696f6e2d636c61737369666965722f64616f2d6d6f6465726174696f6e2d6633322e676775660b2d2d6e6f2d7761726d7570022d6303353132";
  const std::string LOAD_BAD_POOLING =
      "4449444c026c01dd9ad28304016d71010004072d2d6d6f64656c3f6d6f64656c732f6f6e696361692f64616f2d6d6f6465726174696f6e2d636c61737369666965722f64616f2d6d6f6465726174696f6e2d6633322e67677566092d2d706f6f6c696e67076d6178696d756d";
  const std::string LOAD_EMBEDDING_ONLY =
      "4449444c026c01dd9ad28304016d71010006072d2d6d6f64656c3f6d6f64656c732f6f6e696361692f64616f2d6d6f6465726174696f6e2d636c61737369666965722f64616f2d6d6f6465726174696f6e2d6633322e676775660b2d2d656d62656464696e670b2d2d6e6f2d7761726d7570022d6303353132";
  const std::string LOAD_STORIES_AS_CLASSIFIER =
      "4449444c026c01dd9ad28304016d71010006072d2d6d6f64656c1d6d6f64656c732f73746f726965733236304b746f6b3531322e676775660b2d2d656d62656464696e67092d2d706f6f6c696e670472616e6b0b2d2d6e6f2d7761726d7570";
  const std::string LOAD_STORIES =
      "4449444c026c01dd9ad28304016d71010003072d2d6d6f64656c1d6d6f64656c732f73746f726965733236304b746f6b3531322e676775660b2d2d6e6f2d7761726d7570";
  const std::string TEXT_QUESTION =
      "4449444c016c01ad99e7e70471010048686f77206c6f6e6720697320746865206d696e20646973736f6c76652064656c617920746f20766f74652077697468206d79206e6575726f6e3f206e6577206865726520f09f998f";
  const std::string TEXT_TROLL =
      "4449444c016c01ad99e7e7047101003c6c6f6c20696d6167696e65207374616b696e6720666f72203820796561727320696e206120636f696e2072756e20627920636c6f776e7320f09fa4a1";
  const std::string TEXT_SCAM =
      "4449444c016c01ad99e7e7047101006048692c2049276d2066726f6d20746865207465616d20737570706f7274206465736b2e20444d206d6520796f757220697373756520616e642049276c6c2066697820796f757220737475636b207374616b65206469726563746c7920f09f998f";
  const std::string TEXT_EMPTY = "4449444c016c01ad99e7e70471010000";
  // 8193 bytes: one more than CLASSIFY_MAX_TEXT_BYTES (leb128 8193 = 81 40)
  std::string TEXT_TOO_LONG = "4449444c016c01ad99e7e7047101008140";
  for (int i = 0; i < 8193; i++) {
    TEXT_TOO_LONG += "61";
  }
  const std::string NEW_CHAT =
      "4449444c026c01dd9ad28304016d710100020e2d2d70726f6d70742d63616368650e636c6173736966792e6361636865";
  const std::string RUN_UPDATE_HI =
      "4449444c026c01dd9ad28304016d710100060e2d2d70726f6d70742d63616368650e636c6173736966792e6361636865022d70026869022d6e0131";
  // run_decision request with one choice question (test_decision's INTENT_ONLY)
  const std::string DECISION_REQUEST =
      "4449444c076c0391ecada008018dcddc9e0b02dee6f8ff0d056b02c8dc858a0371cdf1cbbe03716d036c03dbb70171d4c2a7b80404a5adfee906716b03d8b1a8c8047fd2e6e5c6077fe1febe850c7f6d066c039f93c60271fc91f4f8057194a5a3a60b7101000098017b226d657373616765223a202248692c204920776173206368617267656420747769636520666f72206d79206f7264657220233434373120616e6420492077616e74206120726566756e642e222c2022706c616e223a202270726f222c20226f72646572223a207b226964223a20343437312c20226974656d73223a205b2270686f6e652063617365222c202263686172676572225d7d7d0106696e74656e74021c5768617420646f65732074686520637573746f6d65722077616e743f0406726566756e641077616e7473206d6f6e6579206261636b06696e74656e740663616e63656c1877616e747320746f2063616e63656c20616e206f7264657206696e74656e7405747261636b1f77616e747320746f206b6e6f7720776865726520616e206f7264657220697306696e74656e74056f746865720d616e797468696e6720656c736506696e74656e74";
  // "The loaded model is a sequence classifier: use run_classify."
  const std::string CLASSIFIER_ERROR_HEX =
      "546865206c6f61646564206d6f64656c20697320612073657175656e636520636c61737369666965723a207573652072756e5f636c6173736966792e";
  const std::string NOT_A_CLASSIFIER =
      "the loaded model is not a sequence classifier: load it with "
      "--embedding --pooling rank";

  // upstream llama-embedding, with the f16 GGUF
  const std::vector<double> REF_QUESTION{4.811, 1.1,    1.718,  0.549,  2.663,
                                         3.253, -0.994, -6.576, -5.15,  -6.264,
                                         3.78,  4.253,  0.197,  -3.436, -3.588};
  const std::vector<double> REF_TROLL{-2.715, 0.466,  -3.664, 4.776,  0.016,
                                      -2.278, 0.731,  -2.751, -7.644, -4.131,
                                      -2.92,  -1.452, 1.862,  -1.758, -1.314};
  const std::vector<double> REF_SCAM{0.851,  -1.182, -3.551, -1.102, 10.701,
                                     4.523,  -3.043, -1.164, 6.606,  4.956,
                                     -9.664, -4.282, -6.804, 2.308,  6.073};
  const double TOL = 0.02;

  // --- a bad --pooling value is refused by load_model
  {
    std::string out;
    mockIC.run_test("test_classify: load with a bad --pooling", load_model,
                    LOAD_BAD_POOLING, "", silent_on_trap, controller, &out);
    check(out.find("2d2d706f6f6c696e673a206f6e6c792072616e6b") !=
              std::string::npos,
          "bad --pooling: expected the 'only rank' error");
  }
  // --embedding without --pooling rank would leave an embeddings context that
  // nothing serves: refused
  {
    std::string out;
    mockIC.run_test("test_classify: load with --embedding only", load_model,
                    LOAD_EMBEDDING_ONLY, "", silent_on_trap, controller, &out);
    check(out.find("676f20746f676574686572") != std::string::npos,
          "--embedding only: expected the 'go together' error");
  }

  // --- loaded without --embedding --pooling rank, it is no classifier
  mockIC.run_test("test_classify: load without rank pooling", load_model,
                  LOAD_CLASSIFIER_NO_POOLING, "", silent_on_trap, controller);
  expect_error(mockIC, "without rank pooling", TEXT_QUESTION, controller,
               NOT_A_CLASSIFIER);

  // --- the classifier
  mockIC.run_test("test_classify: load Ettin-17M", load_model, LOAD_CLASSIFIER,
                  "", silent_on_trap, controller);
  check(classify_model_loaded(), "classify_model_loaded() after the load");

  const ClassifyReply question =
      call(mockIC, "question", TEXT_QUESTION, controller);
  check_scores(question, "question", REF_QUESTION, TOL);
  check_scores(call(mockIC, "troll", TEXT_TROLL, controller), "troll",
               REF_TROLL, TOL);
  check_scores(call(mockIC, "scam", TEXT_SCAM, controller), "scam", REF_SCAM,
               TOL);
  check(question.labels.size() == 15 && question.labels[0] == "kind:question" &&
            question.labels[5] == "toxicity:0",
        "the labels are the GGUF's classifier labels, in order");

  // the same text again, and as a query: the same scores
  {
    const ClassifyReply again =
        call(mockIC, "question again", TEXT_QUESTION, controller);
    check(again.scores == question.scores,
          "question again: the scores changed");
    const ClassifyReply query =
        call(mockIC, "question as a query", TEXT_QUESTION, controller,
             run_classify_query);
    check(query.scores == question.scores,
          "run_classify_query: the scores differ from run_classify");
  }

  // --- access
  {
    const ClassifyReply r = call(mockIC, "anonymous", TEXT_QUESTION, anonymous);
    check(r.label == "Err" && r.err_text == "Access Denied",
          "anonymous: expected Access Denied, got " + r.err_text);
    const ClassifyReply q = call(mockIC, "anonymous query", TEXT_QUESTION,
                                 anonymous, run_classify_query);
    check(q.label == "Err" && q.err_text == "Access Denied",
          "anonymous query: expected Access Denied, got " + q.err_text);
  }

  // --- input checks (no forward pass is run)
  expect_error(mockIC, "empty text", TEXT_EMPTY, controller, "text is empty");
  expect_error(mockIC, "text too long", TEXT_TOO_LONG, controller,
               "text is 8193 bytes, more than the limit of 8192");

  // --- no generation on a classifier, and no decisions
  for (const auto &[name, method, input] :
       std::vector<std::tuple<std::string, void (*)(), std::string>>{
           {"new_chat", new_chat, NEW_CHAT},
           {"run_update", run_update, RUN_UPDATE_HI}}) {
    std::string out;
    mockIC.run_test("test_classify: " + name + " on a classifier", method,
                    input, "", silent_on_trap, controller, &out);
    check(out.find(CLASSIFIER_ERROR_HEX) != std::string::npos,
          name + ": expected the 'use run_classify' error");
  }
  {
    std::string out;
    mockIC.run_test("test_classify: run_decision on a classifier", run_decision,
                    DECISION_REQUEST, "", silent_on_trap, controller, &out);
    // "not a decision model"
    check(out.find("6e6f742061206465636973696f6e206d6f64656c") !=
              std::string::npos,
          "run_decision: expected the 'not a decision model' error");
  }

  // --- a text longer than the context: an error, not a trap
  mockIC.run_test("test_classify: load with -c 8", load_model,
                  LOAD_CLASSIFIER_CTX_8, "", silent_on_trap, controller);
  {
    const ClassifyReply r =
        call(mockIC, "longer than -ub", TEXT_QUESTION, controller);
    check(r.label == "Err" &&
              r.err_text.find("tokens, more than the 8 of the loaded "
                              "context") != std::string::npos,
          "longer than -ub: expected the token limit error, got " + r.err_text);
  }

  // --- a model without a classification head, loaded as a classifier: refused,
  //     and no model stays loaded
  {
    std::string out;
    mockIC.run_test("test_classify: load stories260K as a classifier",
                    load_model, LOAD_STORIES_AS_CLASSIFIER, "", silent_on_trap,
                    controller, &out);
    check(out.find("6e6f20636c617373696669636174696f6e2068656164") !=
              std::string::npos,
          "stories260K as a classifier: expected the 'no classification head' "
          "error");
    expect_error(mockIC, "after a refused load", TEXT_QUESTION, controller,
                 "no model loaded");
  }

  // --- a chat model afterwards: no classifier, and generation works again
  mockIC.run_test("test_classify: load stories260K", load_model, LOAD_STORIES,
                  "", silent_on_trap, controller);
  check(!classify_model_loaded(), "classify_model_loaded() after a chat model");
  expect_error(mockIC, "chat model", TEXT_QUESTION, controller,
               NOT_A_CLASSIFIER);
  {
    std::string out;
    mockIC.run_test("test_classify: run_update on the chat model", run_update,
                    RUN_UPDATE_HI, "", silent_on_trap, controller, &out);
    check(out.find(CLASSIFIER_ERROR_HEX) == std::string::npos,
          "run_update on the chat model: still the classifier error");
  }

  if (g_failures > 0) {
    std::cout << "test_classify: " << g_failures << " check(s) failed"
              << std::endl;
    std::exit(1);
  }
}
