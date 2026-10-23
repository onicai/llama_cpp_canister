// Native (MockIC) test of run_update_full: ONE update call that ingests the
// prompt and generates the whole response, yielding to itself between chunks.
//
// The reference is the multi-call loop a client runs today with run_update:
// ingest until prompt_remaining is empty, then generate until generated_eog.
// With greedy sampling both must produce exactly the same output.
//
// The Mock IC delivers the yields natively: run_test returns after the whole
// coroutine ran.

#include "test_run_update_full.h"

#include "../src/db_chats.h"
#include "../src/max_tokens.h"
#include "../src/model.h"
#include "../src/promptcache.h"
#include "../src/run.h"

#include "ic0.h"
#include "mock_ic.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace {

const std::string MODEL = "models/stories15Mtok4096.gguf";
const std::string PROMPT = "Joe loves writing stories";
const std::string PROMPT_B = "Once upon a time, a little cat";
// n times "cat"
std::string cats(int n) {
  std::string s = "cat";
  for (int i = 1; i < n; ++i)
    s += " cat";
  return s;
}
// 129 tokens, BOS included: one more than the batch of the context
const std::string PROMPT_ONE = cats(128);
// The tokens of the response: stories15M rarely ends a story with an eog.
const uint64_t N = 30;

// An InputRecord with these args, as hex.
std::string args_hex(const std::vector<std::string> &args) {
  std::vector<std::string> a = args;
  CandidTypeRecord r;
  r.append("args", CandidTypeVecText{a});
  return CandidSerialize(r).as_hex_string();
}

// The fields of a reply that the tests compare.
struct Out {
  std::string conversation;
  std::string output;
  std::string prompt_remaining;
  bool generated_eog = false;
  uint64_t n_prompt_tokens = 0;
  uint64_t n_tokens_generated = 0;
  bool partial = false;
};

// full: a run_update_full reply, which also carries `partial`.
Out decode_out(const std::string &hex, bool full = false) {
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
  std::optional<uint64_t> n_prompt;
  r.append("n_prompt_tokens", CandidTypeOptNat64{&n_prompt});
  std::optional<uint64_t> n_gen;
  r.append("n_tokens_generated", CandidTypeOptNat64{&n_gen});
  std::optional<bool> partial;
  if (full) r.append("partial", CandidTypeOptBool{&partial});
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
    std::cout << "run_update_full test: Err - " << error << std::endl;
    exit(1);
  }
  o.n_prompt_tokens = n_prompt.value_or(0);
  o.n_tokens_generated = n_gen.value_or(0);
  o.partial = partial.value_or(false);
  return o;
}

// The args of a greedy run on `cache`, with prompt and -n.
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

// The saved chats of a principal: file name -> content.
std::map<std::string, std::string> saved_chats(const std::string &principal) {
  std::map<std::string, std::string> chats;
  std::string dir, error_msg;
  if (!get_saved_chats_dir(principal, dir, error_msg)) return chats;
  std::error_code ec;
  for (const auto &entry : std::filesystem::directory_iterator(dir, ec)) {
    std::ifstream f(entry.path());
    chats[entry.path().filename().string()] =
        std::string(std::istreambuf_iterator<char>(f), {});
  }
  return chats;
}

// Whether a reply holds `text`, Candid-encoded as hex.
bool has_text(const std::string &hex, const std::string &text) {
  static const char *digits = "0123456789abcdef";
  std::string text_hex;
  for (unsigned char ch : text) {
    text_hex += digits[ch >> 4];
    text_hex += digits[ch & 0xf];
  }
  return hex.find(text_hex) != std::string::npos;
}

// Reports a check, and stops the test run when it failed.
void check(bool ok, const std::string &what) {
  if (!ok) {
    std::cout << "Test 'test_run_update_full: " << what << "' Failed\n";
    exit(1);
  }
  std::cout << "Test 'test_run_update_full: " << what << "' - Passed\n";
}

} // namespace

// The status code of a reply, Ok or Err.
uint16_t status_of(const std::string &hex) {
  uint16_t status_code = 0;
  std::string text;
  bool eog = false;
  CandidTypeRecord r;
  r.append("status_code", CandidTypeNat16{&status_code});
  r.append("conversation", CandidTypeText{&text});
  r.append("output", CandidTypeText{&text});
  r.append("error", CandidTypeText{&text});
  r.append("prompt_remaining", CandidTypeText{&text});
  r.append("generated_eog", CandidTypeBool{&eog});
  std::string label;
  CandidTypeVariant v{&label};
  v.append("Ok", r);
  v.append("Err", r);
  VecBytes bytes;
  bytes.store_hex_string(hex);
  CandidArgs args;
  args.append(v);
  CandidDeserialize(bytes, args);
  return status_code;
}

// run_update_full against a run_update loop, and with other calls in between.
void test_run_update_full(MockIC &mockIC) {
  std::string my_principal{MOCKIC_CONTROLLER};
  bool silent_on_trap = true;
  std::string out_hex;

  std::cout << "\n========== test_run_update_full ==========\n";

  mockIC.run_test("test_run_update_full: load_model", load_model,
                  args_hex({"--model", MODEL}), "", silent_on_trap,
                  my_principal);

  // A small budget per call, so the prompt and the response take many chunks.
  // '(record { max_tokens_query = 4 : nat64; max_tokens_update = 4 : nat64 })'
  mockIC.run_test(
      "test_run_update_full: set_max_tokens 4", set_max_tokens,
      "4449444c016c02deb5daad0478f3a29d8e077801000400000000000000040000000000"
      "0000",
      "", silent_on_trap, my_principal);

  // --- The reference: the multi-call loop of today
  mockIC.run_test("test_run_update_full: new_chat ref", new_chat,
                  args_hex({"--prompt-cache", "ref.cache"}), "", silent_on_trap,
                  my_principal);
  std::string ref;
  bool eog = false;
  uint64_t generated = 0;
  int calls = 0;
  // Every call's output is part of the response, and its n_tokens_generated
  // counts the tokens of that output. A special token sampled without -sp is
  // counted but adds no text, so only text without a count is wrong.
  bool outputs_match_counts = true;
  auto add_call = [&](const Out &o) {
    ref += o.output;
    eog = o.generated_eog;
    generated += o.n_tokens_generated;
    outputs_match_counts =
        outputs_match_counts && (o.output.empty() || o.n_tokens_generated > 0);
  };
  // ingest with -n 1. The call that ingests the end of the prompt samples a
  // token, and decodes it into the session file: it starts the response.
  Out last_ingest;
  while (calls < 100) {
    ++calls;
    mockIC.run_test("test_run_update_full: run_update ref ingest", run_update,
                    args_hex(run_args("ref.cache", PROMPT, "1")), "",
                    silent_on_trap, my_principal, &out_hex);
    last_ingest = decode_out(out_hex);
    add_call(last_ingest);
    if (last_ingest.prompt_remaining.empty()) break;
  }
  check(last_ingest.n_tokens_generated == 1,
        "the last ingest call generates the token -n 1 asks for");
  // generate, empty prompt, N tokens in total.
  while (calls < 100 && !eog && generated < N) {
    ++calls;
    const uint64_t r = N - generated;
    mockIC.run_test("test_run_update_full: run_update ref generate", run_update,
                    args_hex(run_args("ref.cache", "", std::to_string(r))), "",
                    silent_on_trap, my_principal, &out_hex);
    add_call(decode_out(out_hex));
  }
  check(eog || generated == N, "reference loop: " + std::to_string(generated) +
                                   " tokens in " + std::to_string(calls) +
                                   " calls");
  check(outputs_match_counts,
        "reference loop: a call has output exactly when it generated tokens");

  // --- One call
  mockIC.run_test("test_run_update_full: new_chat full", new_chat,
                  args_hex({"--prompt-cache", "full.cache"}), "",
                  silent_on_trap, my_principal);
  mockIC.run_test("test_run_update_full: run_update_full", run_update_full,
                  args_hex(run_args("full.cache", PROMPT, std::to_string(N))),
                  "", silent_on_trap, my_principal, &out_hex);
  Out full = decode_out(out_hex, true);
  check(full.prompt_remaining.empty(), "run_update_full ingested the prompt");
  check(full.n_tokens_generated == generated,
        "run_update_full generated " + std::to_string(full.n_tokens_generated) +
            " tokens");
  std::cout << "reference: " << ref << "\nfull     : " << full.output
            << std::endl;
  check(full.output == ref, "run_update_full output == multi-call output");

  // --- -n is the total of the whole response
  mockIC.run_test("test_run_update_full: new_chat n10", new_chat,
                  args_hex({"--prompt-cache", "n10.cache"}), "", silent_on_trap,
                  my_principal);
  mockIC.run_test("test_run_update_full: run_update_full -n 10",
                  run_update_full,
                  args_hex(run_args("n10.cache", PROMPT, "10")), "",
                  silent_on_trap, my_principal, &out_hex);
  Out n10 = decode_out(out_hex, true);
  check(n10.n_tokens_generated == 10 && !n10.output.empty() &&
            ref.rfind(n10.output, 0) == 0,
        "run_update_full -n 10 is a prefix of the full response");

  // --- The prompt is not output, also when its last batch is 1 token.
  // PROMPT_ONE is one token more than the batch of the context, so without a
  // max_tokens cap it is decoded as a batch of 128 tokens and a batch of 1.
  // '(record { max_tokens_query = 0 : nat64; max_tokens_update = 0 : nat64 })'
  mockIC.run_test(
      "test_run_update_full: set_max_tokens 0 (one)", set_max_tokens,
      "4449444c016c02deb5daad0478f3a29d8e077801000000000000000000000000000000"
      "0000",
      "", silent_on_trap, my_principal);
  // The cache starts empty: new_chat keeps a cache of an earlier run.
  mockIC.run_test("test_run_update_full: remove_prompt_cache one",
                  remove_prompt_cache,
                  args_hex({"--prompt-cache", "one.cache"}), "", silent_on_trap,
                  my_principal);
  mockIC.run_test("test_run_update_full: new_chat one", new_chat,
                  args_hex({"--prompt-cache", "one.cache"}), "", silent_on_trap,
                  my_principal);
  mockIC.run_test("test_run_update_full: run_update one", run_update,
                  args_hex(run_args("one.cache", PROMPT_ONE, "1")), "",
                  silent_on_trap, my_principal, &out_hex);
  const Out one = decode_out(out_hex);
  mockIC.run_test(
      "test_run_update_full: set_max_tokens 4 (one)", set_max_tokens,
      "4449444c016c02deb5daad0478f3a29d8e077801000400000000000000040000000000"
      "0000",
      "", silent_on_trap, my_principal);
  check(one.n_prompt_tokens == 129 && one.prompt_remaining.empty(),
        "one: the prompt is 129 tokens, ingested in one call");
  check(one.n_tokens_generated == 1 && !one.output.empty(),
        "one: the call generates 1 token");
  // The conversation is the prompt followed by the output, so the output holds
  // nothing of the prompt.
  check(one.conversation.ends_with(PROMPT_ONE + one.output),
        "one: a 1-token last batch of the prompt is not in the output");

  // --- Interleaving: two generations whose chunks take turns on the shared
  // model context. Each chunk reloads its own session file, so each must end
  // with the same reply as when it ran alone.
  for (const std::string c : {"soloA", "soloB", "ia", "ib"}) {
    mockIC.run_test("test_run_update_full: new_chat " + c, new_chat,
                    args_hex({"--prompt-cache", c + ".cache"}), "",
                    silent_on_trap, my_principal);
  }
  mockIC.run_test("test_run_update_full: solo A", run_update_full,
                  args_hex(run_args("soloA.cache", PROMPT, "20")), "",
                  silent_on_trap, my_principal, &out_hex);
  const Out solo_a = decode_out(out_hex, true);
  mockIC.run_test("test_run_update_full: solo B", run_update_full,
                  args_hex(run_args("soloB.cache", PROMPT_B, "20")), "",
                  silent_on_trap, my_principal, &out_hex);
  const Out solo_b = decode_out(out_hex, true);
  check(!solo_a.output.empty() && solo_a.output != solo_b.output,
        "the solo replies differ");
  mockIC.set_auto_drain(false);
  MockIC::CallHandle ia = mockIC.start_call(
      "test_run_update_full: interleaved A", run_update_full,
      args_hex(run_args("ia.cache", PROMPT, "20")), my_principal);
  MockIC::CallHandle ib = mockIC.start_call(
      "test_run_update_full: interleaved B", run_update_full,
      args_hex(run_args("ib.cache", PROMPT_B, "20")), my_principal);
  check(mockIC.pending_calls() == 2, "both generations yielded");
  mockIC.drain();
  mockIC.set_auto_drain(true);
  check(mockIC.call_done(ia) && !mockIC.was_rejected(ia) &&
            mockIC.call_done(ib) && !mockIC.was_rejected(ib),
        "both interleaved generations replied");
  const Out a = decode_out(mockIC.reply_hex(ia), true);
  const Out b = decode_out(mockIC.reply_hex(ib), true);
  check(a.output == solo_a.output && a.conversation == solo_a.conversation,
        "interleaved A == solo A");
  check(b.output == solo_b.output && b.conversation == solo_b.conversation,
        "interleaved B == solo B");

  // --- Busy: while a run_update_full holds a session, other calls on that
  // session get a 409; calls on another session run.
  mockIC.run_test("test_run_update_full: new_chat busy", new_chat,
                  args_hex({"--prompt-cache", "busy.cache"}), "",
                  silent_on_trap, my_principal);
  mockIC.set_auto_drain(false);
  MockIC::CallHandle holder = mockIC.start_call(
      "test_run_update_full: busy holder", run_update_full,
      args_hex(run_args("busy.cache", PROMPT, "20")), my_principal);
  check(mockIC.pending_calls() == 1, "the holder yielded");
  MockIC::CallHandle second = mockIC.start_call(
      "test_run_update_full: busy run_update_full", run_update_full,
      args_hex(run_args("busy.cache", PROMPT, "20")), my_principal);
  MockIC::CallHandle update = mockIC.start_call(
      "test_run_update_full: busy run_update", run_update,
      args_hex(run_args("busy.cache", "", "4")), my_principal);
  check(mockIC.call_done(second) && status_of(mockIC.reply_hex(second)) == 409,
        "a second run_update_full on a held session gets 409");
  check(mockIC.call_done(update) && status_of(mockIC.reply_hex(update)) == 409,
        "run_update on a held session gets 409");
  // The model must not change under the suspended holder, from any session.
  MockIC::CallHandle reload =
      mockIC.start_call("test_run_update_full: busy load_model", load_model,
                        args_hex({"--model", MODEL}), my_principal);
  check(mockIC.call_done(reload) && status_of(mockIC.reply_hex(reload)) == 409,
        "load_model while a run_update_full is running gets 409");
  std::vector<std::string> model_args = run_args("other.cache", "", "4");
  model_args.insert(model_args.begin(), {"--model", MODEL});
  MockIC::CallHandle swap =
      mockIC.start_call("test_run_update_full: busy run_update --model",
                        run_update, args_hex(model_args), my_principal);
  check(mockIC.call_done(swap) && status_of(mockIC.reply_hex(swap)) == 409,
        "run_update --model while a run_update_full is running gets 409");
  // Nothing may change the held session's files either, nor the saved chats.
  const bool chats_were_active = is_db_chats_active();
  mockIC.run_test("test_run_update_full: busy chats_resume", chats_resume,
                  "4449444c0000", "", silent_on_trap, my_principal);
  const std::map<std::string, std::string> chats_before =
      saved_chats(my_principal);
  for (auto [name, method] :
       {std::pair<const char *, void (*)()>{"new_chat", new_chat},
        std::pair<const char *, void (*)()>{"remove_prompt_cache",
                                            remove_prompt_cache}}) {
    MockIC::CallHandle h = mockIC.start_call(
        std::string("test_run_update_full: busy ") + name, method,
        args_hex({"--prompt-cache", "busy.cache"}), my_principal);
    check(mockIC.call_done(h) && status_of(mockIC.reply_hex(h)) == 409,
          std::string(name) + " on a held session gets 409");
  }
  check(saved_chats(my_principal) == chats_before,
        "a refused new_chat leaves the saved chats as they are");
  if (!chats_were_active) {
    mockIC.run_test("test_run_update_full: busy chats_pause", chats_pause,
                    "4449444c0000", "", silent_on_trap, my_principal);
  }
  // '(record { from = "busy-src.cache"; to = "busy.cache" })'
  MockIC::CallHandle copy = mockIC.start_call(
      "test_run_update_full: busy copy_prompt_cache", copy_prompt_cache,
      "4449444c016c02fbca0171eaca8a9e047101000a627573792e63616368650e627573"
      "792d7372632e6361636865",
      my_principal);
  check(mockIC.call_done(copy) &&
            has_text(mockIC.reply_hex(copy), SESSION_BUSY_ERROR),
        "copy_prompt_cache to a held session is refused");
  // '(record { promptcache = "busy.cache"; chunk = blob "\00";
  //            chunksize = 1 : nat64; offset = 0 : nat64 })'
  MockIC::CallHandle upload = mockIC.start_call(
      "test_run_update_full: busy upload_prompt_cache_chunk",
      upload_prompt_cache_chunk,
      "4449444c026c04ed8c8bae040193affe8106789eacf7bb0a71aec3faa40b786d7b01"
      "00010000000000000000000a627573792e63616368650100000000000000",
      my_principal);
  check(mockIC.call_done(upload) &&
            has_text(mockIC.reply_hex(upload), SESSION_BUSY_ERROR),
        "upload_prompt_cache_chunk to a held session is refused");
  mockIC.drain();
  mockIC.set_auto_drain(true);
  check(mockIC.call_done(holder) &&
            decode_out(mockIC.reply_hex(holder), true).output == solo_a.output,
        "the holder completes as if alone");
  mockIC.run_test("test_run_update_full: run_update after the holder",
                  run_update, args_hex(run_args("busy.cache", "", "4")), "",
                  silent_on_trap, my_principal, &out_hex);
  check(status_of(out_hex) == 200, "the session is free again");

  // --- Expiry: a holder that made no progress for 10 minutes loses its
  // session. Another call can take it, and the old holder, when it resumes,
  // stops without touching the session.
  mockIC.run_test("test_run_update_full: new_chat expiry", new_chat,
                  args_hex({"--prompt-cache", "own.cache"}), "", silent_on_trap,
                  my_principal);
  const uint64_t t0 = IC_API::time();
  ic0mock_set_time_override(t0);
  mockIC.set_auto_drain(false);
  MockIC::CallHandle stale = mockIC.start_call(
      "test_run_update_full: expiry stale holder", run_update_full,
      args_hex(run_args("own.cache", PROMPT, "20")), my_principal);
  check(mockIC.pending_calls() == 1, "the stale holder yielded");
  ic0mock_set_time_override(t0 + 11ULL * 60 * 1'000'000'000ULL);
  MockIC::CallHandle taker = mockIC.start_call(
      "test_run_update_full: expiry new holder", run_update_full,
      args_hex(run_args("own.cache", PROMPT, "20")), my_principal);
  check(mockIC.pending_calls() == 2, "an expired session can be taken");
  mockIC.drain();
  ic0mock_clear_time_override();
  mockIC.set_auto_drain(true);
  check(mockIC.call_done(stale) && status_of(mockIC.reply_hex(stale)) == 409,
        "a holder that lost its session stops with 409");
  check(mockIC.call_done(taker) && status_of(mockIC.reply_hex(taker)) == 200,
        "the new holder completes");

  // --- Bad requests: a full call needs a prompt cache, and loads no model.
  mockIC.run_test("test_run_update_full: no --prompt-cache", run_update_full,
                  args_hex({"-p", PROMPT, "-n", "4"}), "", silent_on_trap,
                  my_principal, &out_hex);
  check(status_of(out_hex) == 400, "a full call without a prompt cache: 400");
  std::vector<std::string> with_model = run_args("model.cache", PROMPT, "4");
  with_model.insert(with_model.begin(), {"--model", MODEL});
  mockIC.run_test("test_run_update_full: --model", run_update_full,
                  args_hex(with_model), "", silent_on_trap, my_principal,
                  &out_hex);
  check(status_of(out_hex) == 400, "a full call with --model: 400");

  // --- Saved chats. db_chats_new names a chat by the wall-clock second, so
  // the chats below are created a second apart.
  {
    const bool chats_were_active = is_db_chats_active();
    mockIC.run_test("test_run_update_full: chats_resume", chats_resume,
                    "4449444c0000", "", silent_on_trap, my_principal);
    auto new_chat_on = [&](const std::string &cache) {
      std::this_thread::sleep_for(std::chrono::milliseconds(1100));
      mockIC.run_test("test_run_update_full: new_chat " + cache, new_chat,
                      args_hex({"--prompt-cache", cache}), "", silent_on_trap,
                      my_principal);
      return saved_chats(my_principal).rbegin()->first; // the newest chat
    };

    // A full call saves to the chat that was the latest when it started,
    // also after a new_chat on another session.
    const std::string chat_x = new_chat_on("chat-x.cache");
    mockIC.set_auto_drain(false);
    MockIC::CallHandle full_x = mockIC.start_call(
        "test_run_update_full: chat full x", run_update_full,
        args_hex(run_args("chat-x.cache", PROMPT, "8")), my_principal);
    check(mockIC.pending_calls() == 1, "chat: the full call yielded");
    const std::string chat_y = new_chat_on("chat-y.cache");
    mockIC.drain();
    mockIC.set_auto_drain(true);
    check(mockIC.call_done(full_x) &&
              status_of(mockIC.reply_hex(full_x)) == 200,
          "chat: the full call completes");
    std::map<std::string, std::string> chats = saved_chats(my_principal);
    check(chats[chat_x].find(PROMPT) != std::string::npos &&
              chats[chat_y].find(PROMPT) == std::string::npos,
          "chat: the full call saved to its own chat, not the newer one");

    // The latest chat is the newest by name, not the last written: chat_x
    // was written last, but a save on chat-y goes to chat_y.
    mockIC.run_test("test_run_update_full: chat run_update y", run_update,
                    args_hex(run_args("chat-y.cache", PROMPT_B, "4")), "",
                    silent_on_trap, my_principal);
    std::map<std::string, std::string> after = saved_chats(my_principal);
    check(after[chat_y] != chats[chat_y] && after[chat_x] == chats[chat_x],
          "chat: a save goes to the newest chat by name");

    // A full call whose chat was cleaned up while it was suspended does not
    // recreate it. A principal keeps 3 chats.
    const std::string chat_z = new_chat_on("chat-z.cache");
    mockIC.set_auto_drain(false);
    MockIC::CallHandle full_z = mockIC.start_call(
        "test_run_update_full: chat full z", run_update_full,
        args_hex(run_args("chat-z.cache", PROMPT, "8")), my_principal);
    check(mockIC.pending_calls() == 1, "chat cleanup: the full call yielded");
    for (const char *c : {"chat-1.cache", "chat-2.cache", "chat-3.cache"})
      new_chat_on(c);
    check(!saved_chats(my_principal).contains(chat_z),
          "chat cleanup: three newer chats clean up the full call's chat");
    mockIC.drain();
    mockIC.set_auto_drain(true);
    check(mockIC.call_done(full_z) &&
              status_of(mockIC.reply_hex(full_z)) == 200,
          "chat cleanup: the full call completes");
    after = saved_chats(my_principal);
    check(!after.contains(chat_z) && after.size() == 3,
          "chat cleanup: the full call does not recreate its chat");

    if (!chats_were_active) {
      mockIC.run_test("test_run_update_full: chats_pause", chats_pause,
                      "4449444c0000", "", silent_on_trap, my_principal);
    }
  }

  // --- Stopping: the yield returns CanisterStopping, and the call replies
  // with what it has instead of continuing.
  mockIC.run_test("test_run_update_full: new_chat stop", new_chat,
                  args_hex({"--prompt-cache", "stop.cache"}), "",
                  silent_on_trap, my_principal);
  mockIC.set_auto_drain(false);
  MockIC::CallHandle st = mockIC.start_call(
      "test_run_update_full: stop mid-generation", run_update_full,
      args_hex(run_args("stop.cache", PROMPT, std::to_string(N))),
      my_principal);
  check(mockIC.pending_calls() == 1, "the generation yielded");
  mockIC.set_canister_status(MOCKIC_CANISTER_STATUS_STOPPING);
  mockIC.drain();
  mockIC.set_canister_status(MOCKIC_CANISTER_STATUS_RUNNING);
  mockIC.set_auto_drain(true);
  check(mockIC.call_done(st) && !mockIC.was_rejected(st),
        "a stopping canister replies to the generation");
  const Out stopped = decode_out(mockIC.reply_hex(st), true);
  check(stopped.partial, "the reply of a stopped generation is partial");
  check(!full.partial, "a complete generation is not partial");
  check(stopped.n_tokens_generated < N && ref.rfind(stopped.output, 0) == 0,
        "the reply of a stopped generation is a prefix of the full response");
  mockIC.run_test("test_run_update_full: run_update after the stop", run_update,
                  args_hex(run_args("stop.cache", "", "4")), "", silent_on_trap,
                  my_principal, &out_hex);
  check(status_of(out_hex) == 200, "a stopped generation frees its session");

  // Restore the default budget for the tests that follow
  // '(record { max_tokens_query = 0 : nat64; max_tokens_update = 0 : nat64 })'
  mockIC.run_test(
      "test_run_update_full: set_max_tokens 0", set_max_tokens,
      "4449444c016c02deb5daad0478f3a29d8e077801000000000000000000000000000000"
      "0000",
      "", silent_on_trap, my_principal);
}
