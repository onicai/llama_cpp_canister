// Regression test: a prompt cache written by an older llama.cpp, whose stamp
// still names the current model and context layout, must be discarded (cold
// start), not fail on every call.
//
// v0.20.0 (llama.cpp b11476, LLAMA_SESSION_VERSION 9 -> 11) kept the v3 stamp
// of v0.19.1, so a v0.19.1 cache passed the stamp check, llama_state_load_file
// rejected it, and run_update returned "failed to load session file" until the
// cleanup timer removed the file. The stamp now carries llama.cpp's session
// version (prompt_cache_format()), so such a cache reads as stale.

#include "test_prompt_cache_format.h"

#include "../src/model.h"
#include "../src/promptcache.h"
#include "../src/run.h"

#include "hash-library/sha256.h"

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

static const std::string FAKE_SESSION =
    "a session file written by an older llama.cpp";

void test_prompt_cache_format(MockIC &mockIC) {
  std::string controller{MOCKIC_CONTROLLER};
  bool silent_on_trap = true;

  // record { args = vec {"--model"; "models/stories260Ktok512.gguf"; "--no-warmup"} }
  mockIC.run_test(
      "test_prompt_cache_format: load stories260K", load_model,
      "4449444c026c01dd9ad28304016d71010003072d2d6d6f64656c1d6d6f64656c732f73746f726965733236304b746f6b3531322e676775660b2d2d6e6f2d7761726d7570",
      "", silent_on_trap, controller);

  // A cache + stamp as v0.19.1 leaves them: format line v3, but the current
  // model and context layout (so only the format line can reject it).
  const std::string dir = ".canister_cache/" + controller + "/sessions/";
  const std::string cache = dir + "stale_v3.cache";
  std::filesystem::create_directories(dir);
  {
    std::ofstream f(cache, std::ios::binary | std::ios::trunc);
    f << FAKE_SESSION;
  }
  {
    std::ofstream f(cache + ".icppfmt", std::ios::trunc);
    f << "llama_cpp_canister-prompt-cache-v3" << std::endl;
    f << prompt_cache_model_id() << std::endl;
    f << prompt_cache_layout_id() << std::endl;
  }

  // record { args = vec {"--prompt-cache"; "stale_v3.cache"; "--prompt-cache-all";
  //                      "-p"; "Joe loves writing stories"; "-n"; "2"} }
  mockIC.run_test(
      "test_prompt_cache_format: run_update on a v3 cache", run_update,
      "4449444c026c01dd9ad28304016d710100070e2d2d70726f6d70742d63616368650e7374616c655f76332e6361636865122d2d70726f6d70742d63616368652d616c6c022d70194a6f65206c6f7665732077726974696e672073746f72696573022d6e0132",
      "", silent_on_trap, controller);

  // A successful call replaces the fake bytes with a real session file and
  // rewrites the stamp; a failed one ("failed to load session file") returns
  // before saving, so the fake bytes stay.
  std::string content;
  {
    std::ifstream f(cache, std::ios::binary);
    content.assign(std::istreambuf_iterator<char>(f), {});
  }
  std::string format;
  {
    std::ifstream f(cache + ".icppfmt");
    std::getline(f, format);
  }
  if (content == FAKE_SESSION || format != prompt_cache_format()) {
    std::cout << "FAIL - test_prompt_cache_format: the old cache was not "
                 "discarded (run_update failed on it); stamp '"
              << format << "'" << std::endl;
    std::exit(1);
  }
  std::cout << "test_prompt_cache_format: old cache discarded, cold start, "
               "restamped as '"
            << format << "'" << std::endl;

  std::error_code ec;
  std::filesystem::remove(cache, ec);
  std::filesystem::remove(cache + ".icppfmt", ec);
  std::filesystem::remove(cache + ".logits", ec);
}

// --- restoring a downloaded prompt cache -------------------------------------
//
// A cache downloaded from the canister (bytes + its .icppfmt stamp) and
// uploaded again is stamped by stamp_uploaded_prompt_cache and then loaded
// instead of discarded. Anything it cannot vouch for stays unstamped.

namespace {

const std::string RESTORE = "test_prompt_cache_restore: ";
int g_restore_failures = 0;

void restore_check(bool ok, const std::string &what) {
  if (!ok) {
    ++g_restore_failures;
    std::cout << "FAIL - " << RESTORE << what << std::endl;
  }
}

std::string read_file(const std::string &path) {
  std::ifstream f(path, std::ios::binary);
  return std::string(std::istreambuf_iterator<char>(f), {});
}

std::string sha256_hex(const std::string &bytes) {
  SHA256 sha;
  sha.add(bytes.data(), bytes.size());
  return sha.getHash();
}

// "Ok", or the error text of an Err reply. Its Ok is a StatusCodeRecord, or a
// FileUploadRecord when `upload`. The Ok record is built here, not passed in:
// decoding into a record passed by value left type-table entries behind that
// broke the encoding of the next call's input.
std::string reply_text(const std::string &hex, bool upload) {
  std::string label, err_label, err_text;
  uint16_t err_code{0};
  uint16_t status_code{0};
  std::string filename, filesha256;
  uint64_t filesize{0};
  CandidTypeRecord ok;
  if (upload) {
    ok.append("filename", CandidTypeText{&filename});
    ok.append("filesize", CandidTypeNat64{&filesize});
    ok.append("filesha256", CandidTypeText{&filesha256});
  } else {
    ok.append("status_code", CandidTypeNat16{&status_code});
  }
  CandidTypeVariant err{&err_label};
  err.append("Other", CandidTypeText{&err_text});
  err.append("StatusCode", CandidTypeNat16{&err_code});
  CandidTypeVariant result{&label};
  result.append("Ok", ok);
  result.append("Err", err);
  CandidArgs args;
  args.append(result);
  CandidDeserialize(hex, args);
  if (label == "Ok") return "Ok";
  return err_label == "Other" ? err_text
                              : "StatusCode " + std::to_string(err_code);
}

std::string call(MockIC &mockIC, const std::string &name, void (*fn)(),
                 const CandidTypeRecord &arg, const std::string &caller,
                 bool upload = false) {
  std::string out;
  mockIC.run_test(RESTORE + name, fn, CandidSerialize(arg).as_hex_string(), "",
                  true, caller, &out);
  return reply_text(out, upload);
}

std::string stamp_call(MockIC &mockIC, const std::string &name,
                       const std::string &promptcache, const std::string &stamp,
                       const std::string &sha, const std::string &caller) {
  CandidTypeRecord r;
  r.append("promptcache", CandidTypeText{promptcache});
  r.append("stamp", CandidTypeText{stamp});
  r.append("filesha256", CandidTypeText{sha});
  return call(mockIC, name, stamp_uploaded_prompt_cache, r, caller);
}

// Uploads bytes in chunks; "Ok" or the first error
std::string upload(MockIC &mockIC, const std::string &promptcache,
                   const std::string &bytes, const std::string &caller,
                   size_t n_bytes = std::string::npos) {
  const size_t chunksize = 64 * 1024;
  const size_t end = std::min(n_bytes, bytes.size());
  for (size_t offset = 0; offset == 0 || offset < end; offset += chunksize) {
    const size_t n = std::min(chunksize, end - offset);
    std::vector<uint8_t> chunk(bytes.begin() + offset,
                               bytes.begin() + offset + n);
    CandidTypeRecord r;
    r.append("promptcache", CandidTypeText{promptcache});
    r.append("chunk", CandidTypeVecNat8{chunk});
    r.append("chunksize", CandidTypeNat64{(uint64_t)chunksize});
    r.append("offset", CandidTypeNat64{(uint64_t)offset});
    const std::string reply = call(mockIC, "upload " + promptcache,
                                   upload_prompt_cache_chunk, r, caller, true);
    if (reply != "Ok") return reply;
  }
  return "Ok";
}

struct RunReply {
  std::string label;
  std::string output;
  std::string error;
  uint64_t n_cached{0};
  uint64_t n_decoded{0};
};

RunReply run(MockIC &mockIC, const std::string &name,
             const std::vector<std::string> &args_in,
             const std::string &caller) {
  std::vector<std::string> a = args_in;
  CandidTypeRecord in;
  in.append("args", CandidTypeVecText{a});
  std::string out;
  mockIC.run_test(RESTORE + name, run_update,
                  CandidSerialize(in).as_hex_string(), "", true, caller, &out);
  RunReply rr;
  uint16_t status_code{0};
  std::string conversation, prompt_remaining;
  bool eog{false};
  std::optional<uint64_t> cached, decoded;
  CandidTypeRecord r;
  r.append("status_code", CandidTypeNat16{&status_code});
  r.append("conversation", CandidTypeText{&conversation});
  r.append("output", CandidTypeText{&rr.output});
  r.append("error", CandidTypeText{&rr.error});
  r.append("prompt_remaining", CandidTypeText{&prompt_remaining});
  r.append("generated_eog", CandidTypeBool{&eog});
  r.append("n_prompt_tokens_cached", CandidTypeOptNat64{&cached});
  r.append("n_prompt_tokens_decoded", CandidTypeOptNat64{&decoded});
  CandidTypeVariant v{&rr.label};
  v.append("Ok", r);
  v.append("Err", r);
  CandidArgs args;
  args.append(v);
  CandidDeserialize(out, args);
  rr.n_cached = cached.value_or(0);
  rr.n_decoded = decoded.value_or(0);
  return rr;
}

std::vector<std::string> greedy_args(const std::string &cache,
                                     const std::string &prompt) {
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
          "8"};
}

// A refused stamp leaves the stamp file as it was
void expect_refused(MockIC &mockIC, const std::string &name,
                    const std::string &promptcache, const std::string &stamp,
                    const std::string &sha, const std::string &caller,
                    const std::string &dir, const std::string &expected) {
  const std::string stamp_path = dir + promptcache + ".icppfmt";
  const bool stamped_before = std::filesystem::exists(stamp_path);
  const std::string stamp_before = read_file(stamp_path);
  const std::string reply =
      stamp_call(mockIC, name, promptcache, stamp, sha, caller);
  restore_check(reply.find(expected) != std::string::npos,
                name + ": expected an Err with '" + expected + "', got '" +
                    reply + "'");
  restore_check(std::filesystem::exists(stamp_path) == stamped_before &&
                    read_file(stamp_path) == stamp_before,
                name + ": the stamp changed");
}

// The files of this test, removed before and after it: a cache left by an
// earlier run would be continued instead of started
void remove_restore_files(const std::string &dir) {
  for (const std::string name : {"orig.cache", "ctrl.cache", "straight.cache",
                                 "junk.cache", "badtoken.cache", "x.icppfmt",
                                 "x.logits", "lfm.cache", "lfmup.cache"}) {
    std::error_code ec;
    for (const std::string suffix : {"", ".icppfmt", ".logits"}) {
      std::filesystem::remove(dir + name + suffix, ec);
    }
  }
}

} // namespace

void test_prompt_cache_restore(MockIC &mockIC) {
  const std::string caller{MOCKIC_CONTROLLER};
  const std::string dir = ".canister_cache/" + caller + "/sessions/";
  remove_restore_files(dir);

  {
    std::vector<std::string> a = {"--model", "models/stories15Mtok4096.gguf",
                                  "--no-warmup"};
    CandidTypeRecord in;
    in.append("args", CandidTypeVecText{a});
    mockIC.run_test(RESTORE + "load stories15M", load_model,
                    CandidSerialize(in).as_hex_string(), "", true, caller);
  }

  // A chat, saved in orig.cache with its stamp and logits
  RunReply r =
      run(mockIC, "chat", greedy_args("orig.cache", "Joe loves"), caller);
  restore_check(r.label == "Ok", "chat: " + r.error);
  const std::string bytes = read_file(dir + "orig.cache");
  const std::string stamp = read_file(dir + "orig.cache.icppfmt");
  const std::string sha = sha256_hex(bytes);
  uint32_t n_session = 0;
  if (bytes.size() >= 12) std::memcpy(&n_session, bytes.data() + 8, 4);
  restore_check(n_session > 1 && !stamp.empty(), "chat: no session saved");
  // Every later step edits or replays these bytes: stop here without them
  if (n_session <= 1 || bytes.size() < 16 || stamp.empty()) {
    remove_restore_files(dir);
    std::cout << RESTORE << "setup failed, " << g_restore_failures
              << " failure(s)" << std::endl;
    std::exit(1);
  }

  // The control continues a copy that keeps its stamp and logits
  {
    CandidTypeRecord in;
    in.append("from", CandidTypeText{std::string("orig.cache")});
    in.append("to", CandidTypeText{std::string("ctrl.cache")});
    restore_check(call(mockIC, "copy to the control", copy_prompt_cache, in,
                       caller) == "Ok",
                  "copy to the control");
  }
  const RunReply ctrl = run(mockIC, "continue the control",
                            greedy_args("ctrl.cache", ""), caller);
  restore_check(ctrl.label == "Ok" && !ctrl.output.empty(),
                "continue the control: " + ctrl.error);

  // Download = the bytes on disk; then remove the cache
  {
    std::vector<std::string> a = {"--prompt-cache", "orig.cache"};
    CandidTypeRecord in;
    in.append("args", CandidTypeVecText{a});
    mockIC.run_test(RESTORE + "remove_prompt_cache", remove_prompt_cache,
                    CandidSerialize(in).as_hex_string(), "", true, caller);
  }

  // Sidecars cannot be uploaded or copied
  restore_check(upload(mockIC, "x.icppfmt", stamp, caller).find("sidecar") !=
                    std::string::npos,
                "uploading x.icppfmt was not rejected");
  restore_check(upload(mockIC, "x.logits", stamp, caller).find("sidecar") !=
                    std::string::npos,
                "uploading x.logits was not rejected");
  restore_check(!std::filesystem::exists(dir + "x.icppfmt"),
                "x.icppfmt was written");
  {
    CandidTypeRecord in;
    in.append("from", CandidTypeText{std::string("ctrl.cache.icppfmt")});
    in.append("to", CandidTypeText{std::string("y.cache.icppfmt")});
    restore_check(call(mockIC, "copy a stamp", copy_prompt_cache, in, caller)
                          .find("sidecar") != std::string::npos,
                  "copying a stamp was not rejected");
  }

  // No upload metadata: the control was written by the canister, and keeps
  // its own stamp
  expect_refused(mockIC, "stamp a cache that was not uploaded", "ctrl.cache",
                 stamp, sha256_hex(read_file(dir + "ctrl.cache")), caller, dir,
                 "no completed upload");

  // An incomplete upload: the bytes do not match the sha of the whole file
  restore_check(upload(mockIC, "orig.cache", bytes, caller, bytes.size() / 2) ==
                    "Ok",
                "partial upload");
  expect_refused(mockIC, "stamp an incomplete upload", "orig.cache", stamp, sha,
                 caller, dir, "does not match the uploaded");

  // The complete upload
  restore_check(upload(mockIC, "orig.cache", bytes, caller) == "Ok", "upload");
  restore_check(read_file(dir + "orig.cache") == bytes,
                "the uploaded bytes differ");

  expect_refused(mockIC, "wrong sha", "orig.cache", stamp,
                 sha256_hex(bytes + "x"), caller, dir,
                 "does not match the uploaded");

  // Each stamp line must match
  std::vector<std::string> lines;
  {
    std::istringstream in(stamp);
    for (std::string l; std::getline(in, l);)
      lines.push_back(l);
  }
  restore_check(lines.size() == 3, "the stamp does not have 3 lines");
  if (lines.size() == 3) {
    const char *names[3] = {"format", "model", "context layout"};
    for (int i = 0; i < 3; ++i) {
      std::vector<std::string> edited = lines;
      edited[i] += " edited";
      expect_refused(
          mockIC, std::string("edited ") + names[i] + " line", "orig.cache",
          edited[0] + "\n" + edited[1] + "\n" + edited[2] + "\n", sha, caller,
          dir,
          std::string("the ") + names[i] + " does not match. stamp: '" +
              edited[i] + "'; loaded: '" + lines[i] + "'");
    }
  }

  // Junk with a correct stamp fails the header check
  const std::string junk = "0123456789";
  restore_check(upload(mockIC, "junk.cache", junk, caller) == "Ok",
                "upload junk");
  expect_refused(mockIC, "junk with a correct stamp", "junk.cache", stamp,
                 sha256_hex(junk), caller, dir,
                 "stamp_uploaded_prompt_cache: the file is shorter than a "
                 "session file header");
  const std::string junk16 = "0123456789abcdef";
  restore_check(upload(mockIC, "junk.cache", junk16, caller) == "Ok",
                "upload junk16");
  expect_refused(mockIC, "16 bytes of junk with a correct stamp", "junk.cache",
                 stamp, sha256_hex(junk16), caller, dir, "not a session file");

  // A token outside the vocabulary fails the header check
  {
    std::string bad = bytes;
    const int32_t token = 1000000;
    std::memcpy(bad.data() + 12, &token, 4);
    restore_check(upload(mockIC, "badtoken.cache", bad, caller) == "Ok",
                  "upload badtoken");
    expect_refused(mockIC, "a token outside the vocabulary", "badtoken.cache",
                   stamp, sha256_hex(bad), caller, dir,
                   "outside the vocabulary");
  }

  // The real thing
  restore_check(stamp_call(mockIC, "stamp the upload", "orig.cache", stamp, sha,
                           caller) == "Ok",
                "stamping the uploaded cache was refused");
  restore_check(read_file(dir + "orig.cache.icppfmt") == stamp,
                "the canister's stamp differs from the downloaded one");
  restore_check(!std::filesystem::exists(dir + "orig.cache.logits"),
                "a stamped upload has a logits sidecar");

  // An empty prompt with -n 0 decodes nothing and leaves the restored cache as
  // it is, however often it is called
  for (int i = 0; i < 2; ++i) {
    std::vector<std::string> a = greedy_args("orig.cache", "");
    a.back() = "0";
    r = run(mockIC, "-n 0 on the restored cache", a, caller);
    restore_check(r.label == "Ok", "-n 0 on the restored cache: " + r.error);
    restore_check(r.n_cached == n_session && r.n_decoded == 0,
                  "-n 0 changed the restored cache: cached " +
                      std::to_string(r.n_cached) + ", decoded " +
                      std::to_string(r.n_decoded) + ", session " +
                      std::to_string(n_session));
    restore_check(read_file(dir + "orig.cache") == bytes,
                  "-n 0 rewrote the restored cache");
    restore_check(read_file(dir + "orig.cache.icppfmt") == stamp,
                  "-n 0 dropped the stamp of the restored cache");
  }

  // The restored cache is loaded, not discarded, and continues exactly like the
  // control. Without logits it re-decodes its last token.
  r = run(mockIC, "continue the restored cache", greedy_args("orig.cache", ""),
          caller);
  restore_check(r.label == "Ok", "continue the restored cache: " + r.error);
  restore_check(r.n_cached == n_session - 1 && r.n_decoded == 1,
                "the restored cache was not used: cached " +
                    std::to_string(r.n_cached) + ", decoded " +
                    std::to_string(r.n_decoded) + ", session " +
                    std::to_string(n_session));
  restore_check(r.output == ctrl.output,
                "the restored cache continued with '" + r.output +
                    "', the control with '" + ctrl.output + "'");
  restore_check(std::filesystem::exists(dir + "orig.cache.logits"),
                "the restored cache was saved without logits");
  std::cout << RESTORE << "restored cache continued as the control: '"
            << r.output << "'" << std::endl;

  // Both continue as one straight run of the same chat
  {
    std::vector<std::string> a = greedy_args("straight.cache", "Joe loves");
    a.back() = "16";
    const RunReply straight = run(mockIC, "straight run", a, caller);
    restore_check(straight.output.find(r.output) != std::string::npos,
                  "the straight run '" + straight.output +
                      "' does not continue with '" + r.output + "'");
  }

  // A hybrid model (LFM2.5: recurrent and attention memory) cannot drop just
  // the last token, so it refuses to stamp an upload, and refuses a cache
  // without logits rather than saving a shortened one. Skipped without the
  // model, as in CI.
  const std::string lfm = "models/LiquidAI/LFM2.5-1.2B-Instruct-GGUF/"
                          "LFM2.5-1.2B-Instruct-Q4_K_M.gguf";
  if (!std::filesystem::exists(lfm)) {
    std::cout << RESTORE << "SKIP the hybrid-model checks: " << lfm
              << " not found" << std::endl;
  } else {
    {
      std::vector<std::string> a = {"--model", lfm, "--no-warmup", "--ctx-size",
                                    "512"};
      CandidTypeRecord in;
      in.append("args", CandidTypeVecText{a});
      mockIC.run_test(RESTORE + "load LFM2.5", load_model,
                      CandidSerialize(in).as_hex_string(), "", true, caller);
    }
    r = run(mockIC, "LFM2.5 chat", greedy_args("lfm.cache", "Joe loves"),
            caller);
    restore_check(r.label == "Ok", "LFM2.5 chat: " + r.error);
    const std::string lfm_bytes = read_file(dir + "lfm.cache");
    const std::string lfm_stamp = read_file(dir + "lfm.cache.icppfmt");
    restore_check(!lfm_bytes.empty() && !lfm_stamp.empty(),
                  "LFM2.5 chat: no session saved");

    restore_check(upload(mockIC, "lfmup.cache", lfm_bytes, caller) == "Ok",
                  "LFM2.5 upload");
    const std::string reply =
        stamp_call(mockIC, "stamp an LFM2.5 upload", "lfmup.cache", lfm_stamp,
                   sha256_hex(lfm_bytes), caller);
    restore_check(reply.find("recurrent state") != std::string::npos,
                  "stamping an LFM2.5 upload was not refused: " + reply);
    restore_check(!std::filesystem::exists(dir + "lfmup.cache.icppfmt"),
                  "a refused LFM2.5 upload has a stamp");

    std::error_code ec;
    std::filesystem::remove(dir + "lfm.cache.logits", ec);
    r = run(mockIC, "LFM2.5 cache without logits", greedy_args("lfm.cache", ""),
            caller);
    restore_check(r.label == "Err" &&
                      r.error.find("recurrent memory") != std::string::npos,
                  "LFM2.5 continued a cache without logits: " + r.label + " " +
                      r.error);
    restore_check(read_file(dir + "lfm.cache") == lfm_bytes &&
                      read_file(dir + "lfm.cache.icppfmt") == lfm_stamp,
                  "the refused LFM2.5 cache changed");
  }

  remove_restore_files(dir);

  if (g_restore_failures > 0) {
    std::cout << RESTORE << g_restore_failures << " failure(s)" << std::endl;
    std::exit(1);
  }
}
