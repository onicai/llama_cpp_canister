// Native (MockIC) test: two sessions that alternate run_update calls produce
// the same conversations as when each runs alone.
//
// The model and context are shared by all callers, and a session file holds
// the memory only, not the logits. A call that continues a session decodes
// nothing before its first sample, so it must restore the session's own logits
// from the "<cache>.logits" sidecar; otherwise its first token is sampled from
// the logits of the other session.
//
// Also checks that the sidecar is written, that a session without one still
// continues (and gets one again), and that copy/remove carry the sidecar.

#include "test_alternating_sessions.h"

#include "../src/max_tokens.h"
#include "../src/model.h"
#include "../src/promptcache.h"
#include "../src/run.h"

#include "mock_ic.h"

#include <filesystem>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

namespace {

const std::string MODEL = "models/stories15Mtok4096.gguf";
const std::string PROMPT = "Joe loves writing stories";
const std::string PROMPT_B = "Once upon a time, a little cat";

// The candid-encoded InputRecord for the given args.
std::string args_hex(const std::vector<std::string> &args) {
  std::vector<std::string> a = args;
  CandidTypeRecord r;
  r.append("args", CandidTypeVecText{a});
  return CandidSerialize(r).as_hex_string();
}

// The fields of an OutputRecord that the test compares.
struct Out {
  std::string conversation;
  std::string output;
  std::string prompt_remaining;
  bool generated_eog = false;
  uint64_t n_tokens_generated = 0;
};

// Decodes an OutputRecordResult; exits when it is an Err.
Out decode_out(const std::string &hex) {
  Out o;
  uint16_t status_code = 0;
  std::string error;
  CandidTypeRecord r;
  r.append("status_code", CandidTypeNat16{&status_code});
  r.append("conversation", CandidTypeText{&o.conversation});
  r.append("output", CandidTypeText{&o.output});
  r.append("error", CandidTypeText{&error});
  r.append("prompt_remaining", CandidTypeText{&o.prompt_remaining});
  r.append("generated_eog", CandidTypeBool{&o.generated_eog});
  std::optional<uint64_t> n_gen;
  r.append("n_tokens_generated", CandidTypeOptNat64{&n_gen});
  std::string label;
  CandidTypeVariant v{&label};
  v.append("Ok", r);
  v.append("Err", r);
  VecBytes bytes;
  bytes.store_hex_string(hex);
  CandidArgs args;
  args.append(v);
  CandidDeserialize(bytes, args);
  if (label != "Ok") {
    std::cout << "alternating sessions test: Err - " << error << std::endl;
    exit(1);
  }
  o.n_tokens_generated = n_gen.value_or(0);
  return o;
}

// Greedy run_update args for a session, a prompt and -n.
std::vector<std::string> run_args(const std::string &cache,
                                  const std::string &prompt,
                                  const std::string &n) {
  return {"--prompt-cache",
          cache,
          "--prompt-cache-all",
          "--samplers",
          "temperature",
          "--temp",
          "0.0",
          "-p",
          prompt,
          "-n",
          n};
}

// The conversation stored in a session file: re-read it with a read-only call
// that matches the whole session and samples one token.
std::string stored_conversation(MockIC &mockIC, const std::string &cache,
                                const std::string &principal) {
  std::string hex;
  mockIC.run_test("test_alternating_sessions: read " + cache, run_update,
                  args_hex({"--prompt-cache", cache, "--prompt-cache-all",
                            "--prompt-cache-ro", "--samplers", "temperature",
                            "--temp", "0.0", "-p", "", "-n", "1"}),
                  "", true, principal, &hex);
  return decode_out(hex).conversation;
}

// The candid-encoded CopyPromptCacheInputRecord.
std::string copy_hex(const std::string &from, const std::string &to) {
  std::string f = from;
  std::string t = to;
  CandidTypeRecord r;
  r.append("from", CandidTypeText{f});
  r.append("to", CandidTypeText{t});
  return CandidSerialize(r).as_hex_string();
}

// The logits sidecar of a cache of the given principal.
std::string logits_path(const std::string &principal,
                        const std::string &cache) {
  return ".canister_cache/" + principal + "/sessions/" + cache + ".logits";
}

// Prints the result of a check; exits when it failed.
void check(bool ok, const std::string &what) {
  if (!ok) {
    std::cout << "Test 'test_alternating_sessions: " << what << "' Failed\n";
    exit(1);
  }
  std::cout << "Test 'test_alternating_sessions: " << what << "' - Passed\n";
}

} // namespace

// Two sessions alternating run_update calls equal their solo runs, and the
// logits sidecar follows the cache.
void test_alternating_sessions(MockIC &mockIC) {
  std::string my_principal{MOCKIC_CONTROLLER};
  bool silent_on_trap = true;

  std::cout << "\n========== test_alternating_sessions ==========\n";

  mockIC.run_test("test_alternating_sessions: load_model", load_model,
                  args_hex({"--model", MODEL}), "", silent_on_trap,
                  my_principal);

  // A small budget per call, so a response takes several calls.
  // '(record { max_tokens_query = 4 : nat64; max_tokens_update = 4 : nat64 })'
  mockIC.run_test(
      "test_alternating_sessions: set_max_tokens 4", set_max_tokens,
      "4449444c016c02deb5daad0478f3a29d8e077801000400000000000000040000000000"
      "0000",
      "", silent_on_trap, my_principal);

  for (const std::string c : {"ma", "mb", "sa", "sb", "copy"}) {
    mockIC.run_test("test_alternating_sessions: new_chat " + c, new_chat,
                    args_hex({"--prompt-cache", c + ".cache"}), "",
                    silent_on_trap, my_principal);
  }
  // k < 3: ingest with -n 1; then generate with an empty prompt. Returns the
  // conversation after the call.
  auto step = [&](const std::string &cache, const std::string &prompt, int k) {
    std::string hex;
    mockIC.run_test(
        "test_alternating_sessions: step " + cache, run_update,
        args_hex(run_args(cache, k < 3 ? prompt : "", k < 3 ? "1" : "512")), "",
        silent_on_trap, my_principal, &hex);
    return decode_out(hex).conversation;
  };
  // Runs 8 steps on a session; every generation step must extend it.
  auto run_solo = [&](const std::string &cache, const std::string &prompt) {
    std::string prev;
    for (int k = 0; k < 8; ++k) {
      const std::string conversation = step(cache, prompt, k);
      if (k >= 3) {
        check(conversation.size() > prev.size(),
              cache + " step " + std::to_string(k) + " extends the session");
      }
      prev = conversation;
    }
  };
  run_solo("sa.cache", PROMPT);
  run_solo("sb.cache", PROMPT_B);
  check(std::filesystem::exists(logits_path(my_principal, "sa.cache")),
        "the cache has a logits sidecar");
  for (int k = 0; k < 8; ++k) {
    step("ma.cache", PROMPT, k);
    step("mb.cache", PROMPT_B, k);
  }
  const std::string sa = stored_conversation(mockIC, "sa.cache", my_principal);
  const std::string sb = stored_conversation(mockIC, "sb.cache", my_principal);
  const std::string ma = stored_conversation(mockIC, "ma.cache", my_principal);
  const std::string mb = stored_conversation(mockIC, "mb.cache", my_principal);
  check(!sa.empty() && sa != sb, "the solo conversations differ");
  check(ma == sa, "alternating A == solo A");
  check(mb == sb, "alternating B == solo B");

  // A cache without a sidecar (written by an older build) still continues,
  // and gets a sidecar again.
  std::filesystem::remove(logits_path(my_principal, "ma.cache"));
  const std::string continued = step("ma.cache", PROMPT, 3);
  check(continued.size() > ma.size(), "a cache without a sidecar continues");
  check(std::filesystem::exists(logits_path(my_principal, "ma.cache")),
        "the continued cache has a sidecar again");

  // copy_prompt_cache carries the sidecar along; remove_prompt_cache drops it
  mockIC.run_test("test_alternating_sessions: copy_prompt_cache",
                  copy_prompt_cache, copy_hex("sa.cache", "copy.cache"), "",
                  silent_on_trap, my_principal);
  check(std::filesystem::exists(logits_path(my_principal, "copy.cache")),
        "copy_prompt_cache copies the sidecar");
  check(stored_conversation(mockIC, "copy.cache", my_principal) == sa,
        "the copy continues as the original");
  for (const std::string c : {"ma", "mb", "sa", "sb", "copy"}) {
    mockIC.run_test("test_alternating_sessions: remove_prompt_cache " + c,
                    remove_prompt_cache,
                    args_hex({"--prompt-cache", c + ".cache"}), "",
                    silent_on_trap, my_principal);
    check(!std::filesystem::exists(logits_path(my_principal, c + ".cache")),
          "remove_prompt_cache removes the sidecar of " + c);
  }

  // Restore the default budget for the tests that follow
  // '(record { max_tokens_query = 0 : nat64; max_tokens_update = 0 : nat64 })'
  mockIC.run_test(
      "test_alternating_sessions: set_max_tokens 0", set_max_tokens,
      "4449444c016c02deb5daad0478f3a29d8e077801000000000000000000000000000000"
      "0000",
      "", silent_on_trap, my_principal);
}
