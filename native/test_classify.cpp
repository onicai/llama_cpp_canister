// Native tests for run_classify and run_classify_query, with Ettin-encoder-17M
// (f32):
// a ModernBERT sequence classifier with 18 labels (the guard model of an
// onicai game). The reference scores come from upstream llama.cpp:
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
      "models/onicai/treasure-guard/ettin-encoder-17m-f32.gguf";
  if (!std::filesystem::exists(model)) {
    std::cout << "SKIP - test_classify: " << model << " not found" << std::endl;
    return;
  }
  std::string controller{MOCKIC_CONTROLLER};
  std::string anonymous{"2vxsx-fae"};
  bool silent_on_trap = true;

  const std::string LOAD_CLASSIFIER =
      "4449444c026c01dd9ad28304016d7101000a072d2d6d6f64656c376d6f64656c732f6f6e696361692f74726561737572652d67756172642f657474696e2d656e636f6465722d31376d2d6633322e676775660b2d2d656d62656464696e67092d2d706f6f6c696e670472616e6b0b2d2d6e6f2d7761726d7570022d6303353132032d756203353132";
  const std::string LOAD_CLASSIFIER_CTX_8 =
      "4449444c026c01dd9ad28304016d7101000c072d2d6d6f64656c376d6f64656c732f6f6e696361692f74726561737572652d67756172642f657474696e2d656e636f6465722d31376d2d6633322e676775660b2d2d656d62656464696e67092d2d706f6f6c696e670472616e6b0b2d2d6e6f2d7761726d7570022d630138022d620138032d75620138";
  const std::string LOAD_CLASSIFIER_NO_POOLING =
      "4449444c026c01dd9ad28304016d71010005072d2d6d6f64656c376d6f64656c732f6f6e696361692f74726561737572652d67756172642f657474696e2d656e636f6465722d31376d2d6633322e676775660b2d2d6e6f2d7761726d7570022d6303353132";
  const std::string LOAD_BAD_POOLING =
      "4449444c026c01dd9ad28304016d71010004072d2d6d6f64656c376d6f64656c732f6f6e696361692f74726561737572652d67756172642f657474696e2d656e636f6465722d31376d2d6633322e67677566092d2d706f6f6c696e67076d6178696d756d";
  const std::string LOAD_EMBEDDING_ONLY =
      "4449444c026c01dd9ad28304016d71010006072d2d6d6f64656c376d6f64656c732f6f6e696361692f74726561737572652d67756172642f657474696e2d656e636f6465722d31376d2d6633322e676775660b2d2d656d62656464696e670b2d2d6e6f2d7761726d7570022d6303353132";
  const std::string LOAD_STORIES_AS_CLASSIFIER =
      "4449444c026c01dd9ad28304016d71010006072d2d6d6f64656c1d6d6f64656c732f73746f726965733236304b746f6b3531322e676775660b2d2d656d62656464696e67092d2d706f6f6c696e670472616e6b0b2d2d6e6f2d7761726d7570";
  const std::string LOAD_STORIES =
      "4449444c026c01dd9ad28304016d71010003072d2d6d6f64656c1d6d6f64656c732f73746f726965733236304b746f6b3531322e676775660b2d2d6e6f2d7761726d7570";
  const std::string TEXT_COINS =
      "4449444c016c01ad99e7e7047101003149206772616220612068616e6466756c206f6620636f696e732066726f6d2074686520636865737420616e642072756e21";
  const std::string TEXT_TREASURE =
      "4449444c016c01ad99e7e7047101003343616e204920736565207468652074726561737572653f20492070726f6d697365204920776f6e277420746f7563682069742e";
  const std::string TEXT_SUBNET =
      "4449444c016c01ad99e7e7047101004157687920646964207468652063616e69737465722063726f737320746865207375626e65743f20546f2067657420746f20746865206f74686572206379636c6521";
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
  const std::vector<double> REF_COINS{
      -1.152, 2.17,   -0.543, -0.297, 2.573,  -2.099, -0.778, -0.017, 3.489,
      2.168,  -1.108, 4.771,  0.893,  -1.456, -0.172, -1.96,  -3.033, 3.071};
  const std::vector<double> REF_TREASURE{
      -2.871, -1.228, -0.637, 0.41,   6.737,  1.087,  -0.409, -1.045, 1.464,
      5.313,  -0.636, 1.939,  -0.357, -0.271, -1.915, -2.361, -4.674, -1.642};
  const std::vector<double> REF_SUBNET{
      -2.909, -1.658, -3.459, -0.545, -0.008, 1.759,  -1.394, -3.363, 1.395,
      2.306,  -1.975, 1.888,  0.052,  1.852,  -2.769, -5.095, 3.853,  -1.82};
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
  expect_error(mockIC, "without rank pooling", TEXT_COINS, controller,
               NOT_A_CLASSIFIER);

  // --- the classifier
  mockIC.run_test("test_classify: load Ettin-17M", load_model, LOAD_CLASSIFIER,
                  "", silent_on_trap, controller);
  check(classify_model_loaded(), "classify_model_loaded() after the load");

  const ClassifyReply coins = call(mockIC, "coins", TEXT_COINS, controller);
  check_scores(coins, "coins", REF_COINS, TOL);
  check_scores(call(mockIC, "treasure", TEXT_TREASURE, controller), "treasure",
               REF_TREASURE, TOL);
  check_scores(call(mockIC, "subnet", TEXT_SUBNET, controller), "subnet",
               REF_SUBNET, TOL);
  check(coins.labels.size() == 18 && coins.labels[0] == "intent:gift" &&
            coins.labels[6] == "respect:0",
        "the labels are the GGUF's classifier labels, in order");

  // the same text again, and as a query: the same scores
  {
    const ClassifyReply again =
        call(mockIC, "coins again", TEXT_COINS, controller);
    check(again.scores == coins.scores, "coins again: the scores changed");
    const ClassifyReply query = call(mockIC, "coins as a query", TEXT_COINS,
                                     controller, run_classify_query);
    check(query.scores == coins.scores,
          "run_classify_query: the scores differ from run_classify");
  }

  // --- access
  {
    const ClassifyReply r = call(mockIC, "anonymous", TEXT_COINS, anonymous);
    check(r.label == "Err" && r.err_text == "Access Denied",
          "anonymous: expected Access Denied, got " + r.err_text);
    const ClassifyReply q = call(mockIC, "anonymous query", TEXT_COINS,
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
        call(mockIC, "longer than -ub", TEXT_COINS, controller);
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
    expect_error(mockIC, "after a refused load", TEXT_COINS, controller,
                 "no model loaded");
  }

  // --- a chat model afterwards: no classifier, and generation works again
  mockIC.run_test("test_classify: load stories260K", load_model, LOAD_STORIES,
                  "", silent_on_trap, controller);
  check(!classify_model_loaded(), "classify_model_loaded() after a chat model");
  expect_error(mockIC, "chat model", TEXT_COINS, controller, NOT_A_CLASSIFIER);
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
