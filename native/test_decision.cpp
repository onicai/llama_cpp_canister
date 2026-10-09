// Native tests for run_decision (decision models), with tinylaya-for-testing:
// a slice of Laya published by ggml-org for llama.cpp's own tests. Its outputs
// are near-uniform, so these tests check the mechanics (validation, access,
// ordering, probabilities, resume across calls), not the answers. Answer
// quality is checked by test/test_decision.py with Julia-1 and Laya.
//
// Floats differ between arm64 and x86_64, so the replies are decoded and
// checked value by value instead of comparing Candid hex.
//
// The input hex was encoded with icp-py-core (icp_candid); the PR request is
// the one of llama.cpp PR #29818 (see README-decision-models.md).

#include "test_decision.h"

#include "../src/decision.h"
#include "../src/max_tokens.h"
#include "../src/model.h"
#include "../src/run.h"

#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

namespace {

// A decoded run_decision reply
struct DecisionReply {
  std::string label;     // "Ok" or "Err"
  std::string err_label; // "Other" or "StatusCode"
  std::string err_text;
  uint16_t err_code{0};
  std::vector<std::string> a_ids, a_kinds, a_choice;
  std::vector<double> a_score, a_yes, a_confidence;
  std::vector<std::string> p_question_ids, p_keys;
  std::vector<double> p_probability;
  uint64_t input_tokens{0};
  std::vector<std::string> pending;
  std::optional<uint64_t> state_tokens, state_tokens_remaining; // kev only
};

DecisionReply decode_reply(const std::string &hex) {
  DecisionReply r;
  CandidTypeVariant kind_template;
  kind_template.append("choice", CandidTypeNull{});
  kind_template.append("score", CandidTypeNull{});
  kind_template.append("noul", CandidTypeNull{});

  CandidTypeRecord answers;
  answers.append("id", CandidTypeVecText{&r.a_ids});
  answers.append("kind", CandidTypeVecVariant{&kind_template, &r.a_kinds});
  answers.append("choice", CandidTypeVecText{&r.a_choice});
  answers.append("score", CandidTypeVecFloat64{&r.a_score});
  answers.append("yes", CandidTypeVecFloat64{&r.a_yes});
  answers.append("confidence", CandidTypeVecFloat64{&r.a_confidence});

  CandidTypeRecord probabilities;
  probabilities.append("question_id", CandidTypeVecText{&r.p_question_ids});
  probabilities.append("key", CandidTypeVecText{&r.p_keys});
  probabilities.append("probability", CandidTypeVecFloat64{&r.p_probability});

  CandidTypeRecord ok;
  ok.append("answers", CandidTypeVecRecord{&answers});
  ok.append("probabilities", CandidTypeVecRecord{&probabilities});
  ok.append("input_tokens", CandidTypeNat64{&r.input_tokens});
  ok.append("pending", CandidTypeVecText{&r.pending});
  ok.append("state_tokens", CandidTypeOptNat64{&r.state_tokens});
  ok.append("state_tokens_remaining",
            CandidTypeOptNat64{&r.state_tokens_remaining});

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
    std::cout << "FAIL - test_decision: " << what << std::endl;
  }
}

DecisionReply call(MockIC &mockIC, const std::string &name,
                   const std::string &input, const std::string &caller) {
  std::string out;
  mockIC.run_test(std::string("test_decision: ") + name, run_decision, input,
                  "", false, caller, &out);
  return decode_reply(out);
}

void expect_error(MockIC &mockIC, const std::string &name,
                  const std::string &input, const std::string &caller,
                  const std::string &message) {
  const DecisionReply r = call(mockIC, name, input, caller);
  check(r.label == "Err" && r.err_label == "Other" && r.err_text == message,
        name + ": expected Err '" + message + "', got " + r.label + " '" +
            r.err_text + "'");
}

// Structure of a complete answer to the PR request (5 questions)
void check_pr_answers(const DecisionReply &r, const std::string &name) {
  const std::vector<std::string> ids{"intent", "urgent", "frustration",
                                     "refund", "team"};
  const std::vector<std::string> kinds{"choice", "noul", "score", "noul",
                                       "choice"};
  check(r.label == "Ok", name + ": not Ok: " + r.err_text);
  check(r.a_ids == ids, name + ": answer ids not in request order");
  check(r.a_kinds == kinds, name + ": answer kinds");
  check(r.pending.empty(), name + ": pending not empty");
  for (size_t i = 0; i < r.a_ids.size(); i++) {
    double sum = 0.0, p_best = -1.0;
    std::string best;
    size_t n = 0;
    for (size_t j = 0; j < r.p_keys.size(); j++) {
      if (r.p_question_ids[j] != r.a_ids[i]) continue;
      sum += r.p_probability[j];
      ++n;
      if (r.p_probability[j] > p_best) {
        p_best = r.p_probability[j];
        best = r.p_keys[j];
      }
    }
    check(std::fabs(sum - 1.0) < 1e-6,
          name + ": probabilities of " + r.a_ids[i] + " do not sum to 1");
    const size_t n_expected = r.a_ids[i] == "intent"        ? 4
                              : r.a_ids[i] == "frustration" ? 4
                              : r.a_ids[i] == "team"        ? 10
                                                            : 2;
    check(n == n_expected, name + ": number of options of " + r.a_ids[i]);
    if (r.a_kinds[i] == "choice") {
      check(r.a_choice[i] == best, name + ": choice of " + r.a_ids[i] +
                                       " is not the most probable option");
    }
    if (r.a_kinds[i] == "noul") {
      check(r.a_yes[i] >= 0.0 && r.a_yes[i] <= 1.0,
            name + ": yes of " + r.a_ids[i] + " out of [0, 1]");
    }
    if (r.a_kinds[i] == "score") {
      check(r.a_score[i] >= 0.0 && r.a_score[i] <= 3.0,
            name + ": score of " + r.a_ids[i] + " out of [0, 3]");
    }
  }
}

// The stored kev states of a principal (native runs keep them in
// .canister_cache)
std::vector<std::filesystem::path> stored_states(const std::string &principal) {
  std::vector<std::filesystem::path> out;
  std::error_code ec;
  for (std::filesystem::directory_iterator
           it(".canister_cache/" + principal + "/sessions", ec),
       end;
       !ec && it != end; it.increment(ec)) {
    const std::string name = it->path().filename().string();
    if (name.rfind("decision-state-", 0) == 0 && name.size() > 8 &&
        name.substr(name.size() - 8) == ".session") {
      out.push_back(it->path());
    }
  }
  return out;
}

void remove_stored_states(const std::string &principal) {
  for (const auto &path : stored_states(principal)) {
    std::error_code ec;
    std::filesystem::remove(path, ec);
    std::filesystem::remove(path.string() + ".icppfmt", ec);
  }
}

std::string read_file(const std::filesystem::path &path) {
  std::ifstream in(path, std::ios::binary);
  std::stringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

// Re-sends the request until nothing is pending; returns every reply
std::vector<DecisionReply> call_all(MockIC &mockIC, const std::string &name,
                                    const std::string &input,
                                    const std::string &caller) {
  std::vector<DecisionReply> replies;
  for (int i = 0; i < 30; i++) {
    replies.push_back(
        call(mockIC, name + " " + std::to_string(i + 1), input, caller));
    if (replies.back().label != "Ok" || replies.back().pending.empty()) {
      break;
    }
  }
  return replies;
}

} // namespace

void test_decision(MockIC &mockIC) {
  std::string controller{MOCKIC_CONTROLLER};
  std::string anonymous{"2vxsx-fae"};
  bool silent_on_trap = true;

  const std::string PR_REQUEST =
      "4449444c076c0391ecada008018dcddc9e0b02dee6f8ff0d056b02c8dc858a0371cdf1cbbe03716d036c03dbb70171d4c2a7b80404a5adfee906716b03d8b1a8c8047fd2e6e5c6077fe1febe850c7f6d066c039f93c60271fc91f4f8057194a5a3a60b7101000098017b226d657373616765223a202248692c204920776173206368617267656420747769636520666f72206d79206f7264657220233434373120616e6420492077616e74206120726566756e642e222c2022706c616e223a202270726f222c20226f72646572223a207b226964223a20343437312c20226974656d73223a205b2270686f6e652063617365222c202263686172676572225d7d7d0506696e74656e74021c5768617420646f65732074686520637573746f6d65722077616e743f06757267656e740027446f65732074686973206e65656420612068756d616e2077697468696e2074686520686f75723f0b6672757374726174696f6e011f486f7720667275737472617465642069732074686520637573746f6d65723f06726566756e6400164973206120726566756e64207265717565737465643f047465616d020b5768696368207465616d3f1406726566756e641077616e7473206d6f6e6579206261636b06696e74656e740663616e63656c1877616e747320746f2063616e63656c20616e206f7264657206696e74656e7405747261636b1f77616e747320746f206b6e6f7720776865726520616e206f7264657220697306696e74656e74056f746865720d616e797468696e6720656c736506696e74656e740463616c6d000b6672757374726174696f6e0e6d696c646c7920616e6e6f796564000b6672757374726174696f6e07616e6e6f796564000b6672757374726174696f6e05616e677279000b6672757374726174696f6e0474727565136d6f6e6579206261636b2069732061736b656406726566756e640566616c7365166e6f206d6f6e6579206261636b2069732061736b656406726566756e640762696c6c696e6700047465616d087368697070696e6700047465616d09746563686e6963616c00047465616d0573616c657300047465616d056c6567616c00047465616d0772657475726e7300047465616d05667261756400047465616d086163636f756e747300047465616d09726574656e74696f6e00047465616d056f7468657200047465616d";
  const std::string INTENT_ONLY =
      "4449444c076c0391ecada008018dcddc9e0b02dee6f8ff0d056b02c8dc858a0371cdf1cbbe03716d036c03dbb70171d4c2a7b80404a5adfee906716b03d8b1a8c8047fd2e6e5c6077fe1febe850c7f6d066c039f93c60271fc91f4f8057194a5a3a60b7101000098017b226d657373616765223a202248692c204920776173206368617267656420747769636520666f72206d79206f7264657220233434373120616e6420492077616e74206120726566756e642e222c2022706c616e223a202270726f222c20226f72646572223a207b226964223a20343437312c20226974656d73223a205b2270686f6e652063617365222c202263686172676572225d7d7d0106696e74656e74021c5768617420646f65732074686520637573746f6d65722077616e743f0406726566756e641077616e7473206d6f6e6579206261636b06696e74656e740663616e63656c1877616e747320746f2063616e63656c20616e206f7264657206696e74656e7405747261636b1f77616e747320746f206b6e6f7720776865726520616e206f7264657220697306696e74656e74056f746865720d616e797468696e6720656c736506696e74656e74";
  const std::string ERR_NO_QUESTIONS =
      "4449444c076c0391ecada008018dcddc9e0b02dee6f8ff0d056b02c8dc858a0371cdf1cbbe03716d036c03dbb70171d4c2a7b80404a5adfee906716b03d8b1a8c8047fd2e6e5c6077fe1febe850c7f6d066c039f93c60271fc91f4f8057194a5a3a60b710100010568656c6c6f0000";
  const std::string ERR_UNKNOWN_QUESTION_ID =
      "4449444c076c0391ecada008018dcddc9e0b02dee6f8ff0d056b02c8dc858a0371cdf1cbbe03716d036c03dbb70171d4c2a7b80404a5adfee906716b03d8b1a8c8047fd2e6e5c6077fe1febe850c7f6d066c039f93c60271fc91f4f8057194a5a3a60b710100010568656c6c6f010171020657686963683f0201610001710162000178";
  const std::string ERR_CHOICE_NO_OPTIONS =
      "4449444c076c0391ecada008018dcddc9e0b02dee6f8ff0d056b02c8dc858a0371cdf1cbbe03716d036c03dbb70171d4c2a7b80404a5adfee906716b03d8b1a8c8047fd2e6e5c6077fe1febe850c7f6d066c039f93c60271fc91f4f8057194a5a3a60b710100010568656c6c6f010171020657686963683f00";
  const std::string ERR_SCORE_ONE_LEVEL =
      "4449444c076c0391ecada008018dcddc9e0b02dee6f8ff0d056b02c8dc858a0371cdf1cbbe03716d036c03dbb70171d4c2a7b80404a5adfee906716b03d8b1a8c8047fd2e6e5c6077fe1febe850c7f6d066c039f93c60271fc91f4f8057194a5a3a60b710100010568656c6c6f0101730104486f773f01036c6f77000173";
  const std::string ERR_NOUL_BAD_KEY =
      "4449444c076c0391ecada008018dcddc9e0b02dee6f8ff0d056b02c8dc858a0371cdf1cbbe03716d036c03dbb70171d4c2a7b80404a5adfee906716b03d8b1a8c8047fd2e6e5c6077fe1febe850c7f6d066c039f93c60271fc91f4f8057194a5a3a60b710100010568656c6c6f01016e00045965733f01056d6179626500016e";
  const std::string ERR_BAD_JSON_STATE =
      "4449444c076c0391ecada008018dcddc9e0b02dee6f8ff0d056b02c8dc858a0371cdf1cbbe03716d036c03dbb70171d4c2a7b80404a5adfee906716b03d8b1a8c8047fd2e6e5c6077fe1febe850c7f6d066c039f93c60271fc91f4f8057194a5a3a60b71010000017b010171020657686963683f010161000171";
  const std::string ERR_DUPLICATE_ID =
      "4449444c076c0391ecada008018dcddc9e0b02dee6f8ff0d056b02c8dc858a0371cdf1cbbe03716d036c03dbb70171d4c2a7b80404a5adfee906716b03d8b1a8c8047fd2e6e5c6077fe1febe850c7f6d066c039f93c60271fc91f4f8057194a5a3a60b710100010568656c6c6f020171020657686963683f0171020657686963683f010161000171";
  const std::string LOAD_TINYLAYA =
      "4449444c026c01dd9ad28304016d71010009072d2d6d6f64656c486d6f64656c732f67676d6c2d6f72672f74696e796c6179612d666f722d74657374696e672d676775662f74696e796c6179612d666f722d74657374696e672d51385f302e676775660b2d2d6e6f2d7761726d7570022d630432303438022d620432303438032d75620432303438";
  const std::string LOAD_JULIA_1 =
      "4449444c026c01dd9ad28304016d71010009072d2d6d6f64656c2e6d6f64656c732f67676d6c2d6f72672f4a756c69612d312d474755462f4a756c69612d312d51385f302e676775660b2d2d6e6f2d7761726d7570022d630432303438022d620432303438032d75620432303438";
  const std::string RUN_UPDATE_LOAD_TINYLAYA =
      "4449444c026c01dd9ad28304016d7101000f072d2d6d6f64656c486d6f64656c732f67676d6c2d6f72672f74696e796c6179612d666f722d74657374696e672d676775662f74696e796c6179612d666f722d74657374696e672d51385f302e676775660b2d2d6e6f2d7761726d7570022d630432303438022d620432303438032d756204323034380e2d2d70726f6d70742d63616368650e6465636973696f6e2e6361636865022d70026869022d6e0131";
  const std::string LOAD_STORIES =
      "4449444c026c01dd9ad28304016d71010003072d2d6d6f64656c1d6d6f64656c732f73746f726965733236304b746f6b3531322e676775660b2d2d6e6f2d7761726d7570";
  const std::string MAX_TOKENS_0 =
      "4449444c016c02deb5daad0478f3a29d8e0778010000000000000000000000000000000000";
  const std::string MAX_TOKENS_250 =
      "4449444c016c02deb5daad0478f3a29d8e07780100fa00000000000000fa00000000000000";
  const std::string RUN_UPDATE_HI =
      "4449444c026c01dd9ad28304016d710100060e2d2d70726f6d70742d63616368650e6465636973696f6e2e6361636865022d70026869022d6e0131";
  const std::string MAX_TOKENS_20 =
      "4449444c016c02deb5daad0478f3a29d8e0778010014000000000000001400000000000000";

  // --- a chat model is not a decision model
  mockIC.run_test("test_decision: load stories260K", load_model, LOAD_STORIES,
                  "", silent_on_trap, controller);
  expect_error(mockIC, "chat model", PR_REQUEST, controller,
               "the loaded model is not a decision model");

  // --- load the decision model
  mockIC.run_test("test_decision: load tinylaya", load_model, LOAD_TINYLAYA, "",
                  silent_on_trap, controller);

  // --- access
  {
    const DecisionReply r = call(mockIC, "anonymous", PR_REQUEST, anonymous);
    check(r.label == "Err" && r.err_text == "Access Denied",
          "anonymous: expected Access Denied, got " + r.err_text);
  }

  // --- input validation (no forward pass is run)
  expect_error(mockIC, "no questions", ERR_NO_QUESTIONS, controller,
               "questions: must not be empty");
  expect_error(mockIC, "unknown question id", ERR_UNKNOWN_QUESTION_ID,
               controller, "options: question_id 'x' is not a question id");
  expect_error(mockIC, "choice without options", ERR_CHOICE_NO_OPTIONS,
               controller,
               "questions.q: a choice question needs at least one option");
  expect_error(mockIC, "score with one level", ERR_SCORE_ONE_LEVEL, controller,
               "questions.s: a score question needs 2 to 10 levels");
  expect_error(mockIC, "noul with a bad key", ERR_NOUL_BAD_KEY, controller,
               "questions.n: a noul option key must be 'true' or 'false'");
  expect_error(mockIC, "invalid Json state", ERR_BAD_JSON_STATE, controller,
               "state: Json is not valid JSON");
  expect_error(mockIC, "duplicate question id", ERR_DUPLICATE_ID, controller,
               "questions.q: duplicate id");

  // --- no generation on a decision model: Err 400 "The loaded model is a
  //     decision model: use run_decision."
  mockIC.run_test(
      "test_decision: run_update on a decision model", run_update,
      RUN_UPDATE_HI,
      "4449444c026c06819e846471838fe5800671c897a79907719aa1b2f90c7a"
      "db92a2c90d71cdd9e6b30e7e6b01c5fed20100010100000037546865206c6f61646564206d6f64656c2069732061206465636973696f6e206d6f64656c3a207573652072756e5f6465636973696f6e2e90010000",
      silent_on_trap, controller);

  // --- a decision model loaded by run_update itself (--model in its args):
  //     no generation, and run_decision serves it afterwards
  mockIC.run_test("test_decision: load stories260K again", load_model,
                  LOAD_STORIES, "", silent_on_trap, controller);
  {
    std::string out;
    mockIC.run_test("test_decision: run_update --model tinylaya", run_update,
                    RUN_UPDATE_LOAD_TINYLAYA, "", silent_on_trap, controller,
                    &out);
    const std::string expected_hex_text =
        "72756e5f6465636973696f6e"; // "run_decision" in the error message
    check(out.find(expected_hex_text) != std::string::npos,
          "run_update --model <decision model>: expected the 'use "
          "run_decision' error");
    const DecisionReply r =
        call(mockIC, "after run_update --model", INTENT_ONLY, controller);
    check(r.label == "Ok" && r.pending.empty(),
          "after run_update --model: run_decision did not serve the decision "
          "model: " +
              r.err_text);
  }

  // --- the whole request in one call (no budget)
  mockIC.run_test("test_decision: no budget", set_max_tokens, MAX_TOKENS_0, "",
                  silent_on_trap, controller);
  const DecisionReply full = call(mockIC, "one call", PR_REQUEST, controller);
  check_pr_answers(full, "one call");
  check(full.input_tokens > 0, "one call: input_tokens is 0");

  // --- the same request over several calls (resume)
  mockIC.run_test("test_decision: budget 250", set_max_tokens, MAX_TOKENS_250,
                  "", silent_on_trap, controller);
  DecisionReply r = call(mockIC, "resume 1", PR_REQUEST, controller);
  check(r.label == "Ok" && !r.pending.empty(),
        "resume 1: expected pending questions");
  check(r.input_tokens > 0 && r.input_tokens <= 250,
        "resume 1: input_tokens not within the budget");
  check(r.a_ids.size() + r.pending.size() == 5,
        "resume 1: answers + pending != 5");
  int calls = 1;
  while (r.label == "Ok" && !r.pending.empty() && calls < 10) {
    r = call(mockIC, "resume " + std::to_string(++calls), PR_REQUEST,
             controller);
    check(r.input_tokens <= 250, "resume: input_tokens over the budget");
  }
  check(calls > 1, "resume: the request did not need several calls");
  check_pr_answers(r, "resume final");
  check(r.p_probability == full.p_probability,
        "resume final: probabilities differ from the one-call run");

  // --- a complete request starts fresh: answered again from the start
  {
    const DecisionReply again = call(mockIC, "fresh", PR_REQUEST, controller);
    check(again.label == "Ok" && !again.pending.empty(),
          "fresh: a completed request was not started fresh");
  }

  // --- one question larger than the budget
  mockIC.run_test("test_decision: budget 20", set_max_tokens, MAX_TOKENS_20, "",
                  silent_on_trap, controller);
  {
    const DecisionReply e =
        call(mockIC, "over budget", INTENT_ONLY, controller);
    check(e.label == "Err" &&
              e.err_text.find("more than one update call can evaluate "
                              "(max_tokens_update = 20)") != std::string::npos,
          "over budget: expected the budget error, got " + e.err_text);
  }

  // --- the answers kept between calls belong to the model that computed them:
  //     the "fresh" call above left a partial tinylaya answer of PR_REQUEST.
  //     Julia-1 must answer it itself (tinylaya's answers are near-uniform).
  mockIC.run_test("test_decision: load Julia-1", load_model, LOAD_JULIA_1, "",
                  silent_on_trap, controller);
  mockIC.run_test("test_decision: budget 250 (Julia-1)", set_max_tokens,
                  MAX_TOKENS_250, "", silent_on_trap, controller);
  {
    DecisionReply j = call(mockIC, "other model 1", PR_REQUEST, controller);
    for (int i = 0; i < 10 && j.label == "Ok" && !j.pending.empty(); i++) {
      j = call(mockIC, "other model", PR_REQUEST, controller);
    }
    check_pr_answers(j, "other model");
    double p_refund = 0.0;
    for (size_t k = 0; k < j.p_keys.size(); k++) {
      if (j.p_question_ids[k] == "intent" && j.p_keys[k] == "refund") {
        p_refund = j.p_probability[k];
      }
    }
    check(p_refund > 0.5, "other model: intent was not answered by Julia-1 "
                          "(stale answers of another model?), p(refund) = " +
                              std::to_string(p_refund));
  }

  // --- leave no partial answers behind (native runs share .canister_cache):
  //     complete tinylaya's partial PR_REQUEST, which removes its file
  mockIC.run_test("test_decision: reload tinylaya", load_model, LOAD_TINYLAYA,
                  "", silent_on_trap, controller);
  mockIC.run_test("test_decision: reset budget", set_max_tokens, MAX_TOKENS_0,
                  "", silent_on_trap, controller);
  {
    const DecisionReply done = call(mockIC, "cleanup", PR_REQUEST, controller);
    check(done.label == "Ok" && done.pending.empty(), "cleanup: not complete");
  }

  if (g_failures > 0) {
    std::cout << "test_decision: " << g_failures << " check(s) failed"
              << std::endl;
    std::exit(1);
  }
}

// Kev-0.8B (decision type kev): the state is ingested over calls into a stored
// state per principal, and questions are answered from it. Skipped when the
// model is not downloaded (see the README of Kev-0.8B).
void test_decision_kev(MockIC &mockIC) {
  const std::string model = "models/ggml-org/Kev-0.8B-GGUF/Kev-0.8B-Q8_0.gguf";
  if (!std::filesystem::exists(model)) {
    std::cout << "SKIP - test_decision_kev: " << model << " not found"
              << std::endl;
    return;
  }
  std::string controller{MOCKIC_CONTROLLER};
  bool silent_on_trap = true;

  const std::string KEV_REQUEST =
      "4449444c076c0391ecada008018dcddc9e0b02dee6f8ff0d056b02c8dc858a0371cdf1cbbe03"
      "716d036c03dbb70171d4c2a7b80404a5adfee906716b03d8b1a8c8047fd2e6e5c6077fe1febe"
      "850c7f6d066c039f93c60271fc91f4f8057194a5a3a60b71010001910148692c2049206f7264"
      "6572656420612070616972206f662072756e6e696e672073686f657320286f72646572203838"
      "323133292074656e20646179732061676f2e2054686520747261636b696e6720706167652068"
      "6173207361696420276c6162656c2063726561746564272073696e6365207468652033726420"
      "616e64206e6f7468696e6720686173206d6f7665642e0206696e74656e740207496e74656e74"
      "3f06757267656e740007557267656e743f0606726566756e640006696e74656e740663616e63"
      "656c0006696e74656e7405747261636b0006696e74656e74056f746865720006696e74656e74"
      "047472756506757267656e7406757267656e740566616c73650863616e207761697406757267"
      "656e74";
  const std::string KEV_GROWN_STATE =
      "4449444c076c0391ecada008018dcddc9e0b02dee6f8ff0d056b02c8dc858a0371cdf1cbbe03"
      "716d036c03dbb70171d4c2a7b80404a5adfee906716b03d8b1a8c8047fd2e6e5c6077fe1febe"
      "850c7f6d066c039f93c60271fc91f4f8057194a5a3a60b71010001c10148692c2049206f7264"
      "6572656420612070616972206f662072756e6e696e672073686f657320286f72646572203838"
      "323133292074656e20646179732061676f2e2054686520747261636b696e6720706167652068"
      "6173207361696420276c6162656c2063726561746564272073696e6365207468652033726420"
      "616e64206e6f7468696e6720686173206d6f7665642e0a5570646174653a2074686520706172"
      "63656c206172726976656420746f6461792c20616c6c20676f6f64206e6f772e0106696e7465"
      "6e740207496e74656e743f0406726566756e640006696e74656e740663616e63656c0006696e"
      "74656e7405747261636b0006696e74656e74056f746865720006696e74656e74";
  const std::string KEV_INTENT_ONLY =
      "4449444c076c0391ecada008018dcddc9e0b02dee6f8ff0d056b02c8dc858a0371cdf1cbbe03"
      "716d036c03dbb70171d4c2a7b80404a5adfee906716b03d8b1a8c8047fd2e6e5c6077fe1febe"
      "850c7f6d066c039f93c60271fc91f4f8057194a5a3a60b71010001910148692c2049206f7264"
      "6572656420612070616972206f662072756e6e696e672073686f657320286f72646572203838"
      "323133292074656e20646179732061676f2e2054686520747261636b696e6720706167652068"
      "6173207361696420276c6162656c2063726561746564272073696e6365207468652033726420"
      "616e64206e6f7468696e6720686173206d6f7665642e0106696e74656e740207496e74656e74"
      "3f0406726566756e640006696e74656e740663616e63656c0006696e74656e7405747261636b"
      "0006696e74656e74056f746865720006696e74656e74";
  const std::string LOAD_KEV =
      "4449444c026c01dd9ad28304016d71010009072d2d6d6f64656c306d6f64656c732f67676d6c"
      "2d6f72672f4b65762d302e38422d474755462f4b65762d302e38422d51385f302e676775660b"
      "2d2d6e6f2d7761726d7570022d630438313932022d62023332032d7562023332";
  const std::string LOAD_KEV_CTX_4096 =
      "4449444c026c01dd9ad28304016d71010009072d2d6d6f64656c306d6f64656c732f67676d6c"
      "2d6f72672f4b65762d302e38422d474755462f4b65762d302e38422d51385f302e676775660b"
      "2d2d6e6f2d7761726d7570022d630434303936022d62023332032d7562023332";
  const std::string LOAD_KEV_CTX_256 =
      "4449444c026c01dd9ad28304016d71010009072d2d6d6f64656c306d6f64656c732f67676d6c"
      "2d6f72672f4b65762d302e38422d474755462f4b65762d302e38422d51385f302e676775660b"
      "2d2d6e6f2d7761726d7570022d6303323536022d62023332032d7562023332";
  const std::string KEV_LONG_STATE =
      "4449444c076c0391ecada008018dcddc9e0b02dee6f8ff0d056b02c8dc858a0371cdf1cbbe03"
      "716d036c03dbb70171d4c2a7b80404a5adfee906716b03d8b1a8c8047fd2e6e5c6077fe1febe"
      "850c7f6d066c039f93c60271fc91f4f8057194a5a3a60b71010001c70a48692c2049206f7264"
      "6572656420612070616972206f662072756e6e696e672073686f657320286f72646572203838"
      "323133292074656e20646179732061676f2e2054686520747261636b696e6720706167652068"
      "6173207361696420276c6162656c2063726561746564272073696e6365207468652033726420"
      "616e64206e6f7468696e6720686173206d6f7665642e2049206e656564207468656d20666f72"
      "20612072616365206f6e2053617475726461792e2043616e20796f752074656c6c206d652077"
      "68657265207468652070617263656c2061637475616c6c792069732c206f722073686f756c64"
      "2049206a7573742063616e63656c20616e642062757920736f6d65776865726520656c73653f"
      "20486f6e6573746c79206120626974206469736170706f696e7465642c207468697320697320"
      "6d79207468697264206f72646572207769746820796f752e2048692c2049206f726465726564"
      "20612070616972206f662072756e6e696e672073686f657320286f7264657220383832313329"
      "2074656e20646179732061676f2e2054686520747261636b696e672070616765206861732073"
      "61696420276c6162656c2063726561746564272073696e6365207468652033726420616e6420"
      "6e6f7468696e6720686173206d6f7665642e2049206e656564207468656d20666f7220612072"
      "616365206f6e2053617475726461792e2043616e20796f752074656c6c206d65207768657265"
      "207468652070617263656c2061637475616c6c792069732c206f722073686f756c642049206a"
      "7573742063616e63656c20616e642062757920736f6d65776865726520656c73653f20486f6e"
      "6573746c79206120626974206469736170706f696e7465642c2074686973206973206d792074"
      "68697264206f72646572207769746820796f752e2048692c2049206f72646572656420612070"
      "616972206f662072756e6e696e672073686f657320286f72646572203838323133292074656e"
      "20646179732061676f2e2054686520747261636b696e67207061676520686173207361696420"
      "276c6162656c2063726561746564272073696e6365207468652033726420616e64206e6f7468"
      "696e6720686173206d6f7665642e2049206e656564207468656d20666f722061207261636520"
      "6f6e2053617475726461792e2043616e20796f752074656c6c206d6520776865726520746865"
      "2070617263656c2061637475616c6c792069732c206f722073686f756c642049206a75737420"
      "63616e63656c20616e642062757920736f6d65776865726520656c73653f20486f6e6573746c"
      "79206120626974206469736170706f696e7465642c2074686973206973206d79207468697264"
      "206f72646572207769746820796f752e2048692c2049206f7264657265642061207061697220"
      "6f662072756e6e696e672073686f657320286f72646572203838323133292074656e20646179"
      "732061676f2e2054686520747261636b696e67207061676520686173207361696420276c6162"
      "656c2063726561746564272073696e6365207468652033726420616e64206e6f7468696e6720"
      "686173206d6f7665642e2049206e656564207468656d20666f7220612072616365206f6e2053"
      "617475726461792e2043616e20796f752074656c6c206d652077686572652074686520706172"
      "63656c2061637475616c6c792069732c206f722073686f756c642049206a7573742063616e63"
      "656c20616e642062757920736f6d65776865726520656c73653f20486f6e6573746c79206120"
      "626974206469736170706f696e7465642c2074686973206973206d79207468697264206f7264"
      "6572207769746820796f752e0106696e74656e740207496e74656e743f0406726566756e6400"
      "06696e74656e740663616e63656c0006696e74656e7405747261636b0006696e74656e74056f"
      "746865720006696e74656e74";
  const std::string LOAD_KEV_PARALLEL_2 =
      "4449444c026c01dd9ad28304016d7101000b072d2d6d6f64656c306d6f64656c732f67676d6c"
      "2d6f72672f4b65762d302e38422d474755462f4b65762d302e38422d51385f302e676775660b"
      "2d2d6e6f2d7761726d7570022d630438313932022d62023332032d7562023332032d6e700132";
  const std::string MAX_TOKENS_24 =
      "4449444c016c02deb5daad0478f3a29d8e0778010018000000000000001800000000000000";
  const std::string MAX_TOKENS_10 =
      "4449444c016c02deb5daad0478f3a29d8e077801000a000000000000000a00000000000000";
  const std::string KEV_SMALL_STATE_1 =
      "4449444c076c0391ecada008018dcddc9e0b02dee6f8ff0d056b02c8dc858a0371cdf1cbbe03"
      "716d036c03dbb70171d4c2a7b80404a5adfee906716b03d8b1a8c8047fd2e6e5c6077fe1febe"
      "850c7f6d066c039f93c60271fc91f4f8057194a5a3a60b71010001154f7264657220313a2077"
      "686572652069732069743f0106696e74656e740207496e74656e743f0406726566756e640006"
      "696e74656e740663616e63656c0006696e74656e7405747261636b0006696e74656e74056f74"
      "6865720006696e74656e74";
  const std::string MAX_TOKENS_0 =
      "4449444c016c02deb5daad0478f3a29d8e0778010000000000000000000000000000000000";

  remove_stored_states(controller);
  mockIC.run_test("test_decision_kev: load Kev-0.8B", load_model, LOAD_KEV, "",
                  silent_on_trap, controller);

  // --- the whole request in one call: the reference. The state is ingested
  //     in steps of 8 tokens. A short state and two questions keep this test
  //     fast on the CI's x86 runner.
  mockIC.run_test("test_decision_kev: no budget", set_max_tokens, MAX_TOKENS_0,
                  "", silent_on_trap, controller);
  const DecisionReply full =
      call(mockIC, "kev one call", KEV_REQUEST, controller);
  check(full.label == "Ok" && full.pending.empty() && full.a_ids.size() == 2,
        "kev one call: not complete: " + full.err_text);
  check(full.state_tokens.has_value() && *full.state_tokens > 24 &&
            full.state_tokens_remaining == std::optional<uint64_t>{0},
        "kev one call: state progress");
  const uint64_t n_state = full.state_tokens.value_or(0);
  check(stored_states(controller).size() == 1,
        "kev one call: expected one stored state");

  // --- the same request over several calls, from scratch: the state is
  //     ingested 24 tokens per call (the same 8-token steps as above), then
  //     one question per call. The answers must be identical.
  remove_stored_states(controller);
  mockIC.run_test("test_decision_kev: budget 24", set_max_tokens, MAX_TOKENS_24,
                  "", silent_on_trap, controller);
  const std::vector<DecisionReply> resume =
      call_all(mockIC, "kev resume", KEV_REQUEST, controller);
  const DecisionReply &last = resume.back();
  check(last.label == "Ok" && last.pending.empty(),
        "kev resume: not complete: " + last.err_text);
  check(resume.size() >= 2 + 2, "kev resume: expected >= 2 ingestion calls "
                                "and one call per question");
  uint64_t remaining_before = n_state + 1;
  for (const auto &r : resume) {
    check(r.input_tokens > 0 && r.input_tokens <= 24,
          "kev resume: input_tokens not within the budget");
    const uint64_t remaining = r.state_tokens_remaining.value_or(n_state + 1);
    check(remaining < remaining_before || remaining == 0,
          "kev resume: state_tokens_remaining does not decrease");
    remaining_before = remaining;
  }
  check(last.p_probability == full.p_probability,
        "kev resume: probabilities differ from the one-call run");
  check(last.a_choice == full.a_choice, "kev resume: choices differ");

  // --- a new request about the stored state: only the question is decoded,
  //     and the stored state is not modified
  const std::vector<std::filesystem::path> states = stored_states(controller);
  check(states.size() == 1, "kev reuse: expected one stored state");
  const std::string stored = states.empty() ? "" : read_file(states[0]);
  {
    const DecisionReply r =
        call(mockIC, "kev reuse", KEV_INTENT_ONLY, controller);
    check(r.label == "Ok" && r.pending.empty() && r.a_ids.size() == 1,
          "kev reuse: not answered in one call: " + r.err_text);
    check(r.input_tokens > 0 && r.input_tokens <= 24 &&
              r.state_tokens_remaining == std::optional<uint64_t>{0},
          "kev reuse: the state was ingested again");
    check(r.a_choice.size() == 1 && r.a_choice[0] == full.a_choice[0],
          "kev reuse: intent differs from the full request");
  }
  check(!states.empty() && read_file(states[0]) == stored,
        "kev reuse: answering a question modified the stored state");

  // --- a stored state that is not stamped by this build is discarded
  if (!states.empty()) {
    std::ofstream(states[0].string() + ".icppfmt", std::ios::trunc)
        << "not this build\n";
    const DecisionReply r =
        call(mockIC, "kev stale stamp", KEV_INTENT_ONLY, controller);
    check(r.label == "Ok" && r.state_tokens_remaining.value_or(0) > 0,
          "kev stale stamp: the stale state was used: " + r.err_text);
    call_all(mockIC, "kev stale stamp done", KEV_INTENT_ONLY, controller);
  }

  // --- a state file holding another state (e.g. copied) is discarded: the
  //     stored state of the request above is copied over the one of a short,
  //     unrelated state, which must still get its own answer
  if (!states.empty()) {
    const DecisionReply clean =
        call_all(mockIC, "kev small state", KEV_SMALL_STATE_1, controller)
            .back();
    check(clean.label == "Ok" && clean.pending.empty(),
          "kev small state: not answered: " + clean.err_text);
    for (const auto &other : stored_states(controller)) {
      if (other == states[0]) {
        continue;
      }
      const auto overwrite = std::filesystem::copy_options::overwrite_existing;
      std::error_code ec;
      std::filesystem::copy_file(states[0], other, overwrite, ec);
      std::filesystem::copy_file(states[0].string() + ".icppfmt",
                                 other.string() + ".icppfmt", overwrite, ec);
    }
    const DecisionReply again =
        call_all(mockIC, "kev foreign file", KEV_SMALL_STATE_1, controller)
            .back();
    check(again.label == "Ok" && again.p_probability == clean.p_probability,
          "kev foreign file: the foreign state was used: " + again.err_text);
  }

  // --- validation
  mockIC.run_test("test_decision_kev: budget 10", set_max_tokens, MAX_TOKENS_10,
                  "", silent_on_trap, controller);
  {
    const DecisionReply e =
        call(mockIC, "kev question over budget", KEV_INTENT_ONLY, controller);
    check(e.label == "Err" &&
              e.err_text.find("more than one update call can evaluate "
                              "(max_tokens_update = 10)") != std::string::npos,
          "kev question over budget: got " + e.err_text);
  }
  mockIC.run_test("test_decision_kev: no budget again", set_max_tokens,
                  MAX_TOKENS_0, "", silent_on_trap, controller);
  mockIC.run_test("test_decision_kev: load with -c 256", load_model,
                  LOAD_KEV_CTX_256, "", silent_on_trap, controller);
  {
    const DecisionReply e =
        call(mockIC, "kev state over ctx", KEV_LONG_STATE, controller);
    check(e.label == "Err" &&
              e.err_text.find("--ctx-size") != std::string::npos,
          "kev state over ctx: got " + e.err_text);
  }
  mockIC.run_test("test_decision_kev: load with -np 2", load_model,
                  LOAD_KEV_PARALLEL_2, "", silent_on_trap, controller);
  {
    const DecisionReply e =
        call(mockIC, "kev two sequences", KEV_INTENT_ONLY, controller);
    check(e.label == "Err" &&
              e.err_text.find("one sequence") != std::string::npos,
          "kev two sequences: got " + e.err_text);
  }

  // --- another context layout (-c 4096) does not use the stored state
  mockIC.run_test("test_decision_kev: load with -c 4096", load_model,
                  LOAD_KEV_CTX_4096, "", silent_on_trap, controller);
  mockIC.run_test("test_decision_kev: budget 24 again", set_max_tokens,
                  MAX_TOKENS_24, "", silent_on_trap, controller);
  {
    const DecisionReply r =
        call(mockIC, "kev other layout", KEV_INTENT_ONLY, controller);
    check(r.label == "Ok" && r.state_tokens_remaining.value_or(0) > 0,
          "kev other layout: the state of another layout was used: " +
              r.err_text);
  }

  // --- a grown state (new data appended) continues from the stored state it
  //     starts with: only the new tokens are ingested, and the answer is the
  //     one of the grown state ingested from scratch (up to float noise: the
  //     steps around the old end differ)
  remove_stored_states(controller);
  {
    const std::vector<DecisionReply> first =
        call_all(mockIC, "kev state to grow", KEV_INTENT_ONLY, controller);
    check(first.back().label == "Ok" && first.back().pending.empty(),
          "kev state to grow: not answered: " + first.back().err_text);
    const std::vector<DecisionReply> grown =
        call_all(mockIC, "kev grown state", KEV_GROWN_STATE, controller);
    uint64_t n_evaluated = 0;
    for (const auto &r : grown) {
      n_evaluated += r.input_tokens;
    }
    const DecisionReply &g = grown.back();
    check(g.label == "Ok" && g.pending.empty(),
          "kev grown state: not answered: " + g.err_text);
    check(g.state_tokens.value_or(0) > first.back().state_tokens.value_or(0) &&
              n_evaluated < g.state_tokens.value_or(0),
          "kev grown state: the whole state was ingested again (" +
              std::to_string(n_evaluated) + " tokens evaluated)");
    remove_stored_states(controller);
    const std::vector<DecisionReply> scratch = call_all(
        mockIC, "kev grown state from scratch", KEV_GROWN_STATE, controller);
    const DecisionReply &f = scratch.back();
    check(f.label == "Ok" && f.a_choice == g.a_choice &&
              f.p_probability.size() == g.p_probability.size(),
          "kev grown state: the answer differs from a from-scratch run");
    for (size_t k = 0; k < f.p_probability.size() && k < g.p_probability.size();
         k++) {
      check(std::fabs(f.p_probability[k] - g.p_probability[k]) < 0.01,
            "kev grown state: probability differs from a from-scratch run");
    }
  }

  // --- a principal keeps at most 8 stored states: 8 older ones (dummy files;
  //     eviction only looks at names and mtimes), then one real one
  remove_stored_states(controller);
  {
    const std::string dir = ".canister_cache/" + controller + "/sessions";
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    const auto now = std::filesystem::file_time_type::clock::now();
    for (int i = 0; i < 8; i++) {
      const std::string dummy =
          dir + "/decision-state-dummy" + std::to_string(i) + ".session";
      std::ofstream(dummy) << "not a session file";
      std::filesystem::last_write_time(dummy, now - std::chrono::hours(8 - i),
                                       ec);
    }
  }
  mockIC.run_test("test_decision_kev: no budget (states)", set_max_tokens,
                  MAX_TOKENS_0, "", silent_on_trap, controller);
  {
    const DecisionReply r =
        call(mockIC, "kev ninth state", KEV_SMALL_STATE_1, controller);
    check(r.label == "Ok" && r.pending.empty(),
          "kev ninth state: not answered: " + r.err_text);
    const std::vector<std::filesystem::path> kept = stored_states(controller);
    bool oldest_kept = false;
    for (const auto &path : kept) {
      oldest_kept |= path.filename() == "decision-state-dummy0.session";
    }
    check(kept.size() == 8 && !oldest_kept,
          "kev ninth state: expected the oldest of 9 stored states removed, "
          "got " +
              std::to_string(kept.size()) + " stored states");
  }

  // --- leave nothing behind (native runs share .canister_cache)
  remove_stored_states(controller);

  if (g_failures > 0) {
    std::cout << "test_decision_kev: " << g_failures << " check(s) failed"
              << std::endl;
    std::exit(1);
  }
}
