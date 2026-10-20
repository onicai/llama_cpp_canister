#include "run.h"
#include "auth.h"
#include "common.h"
#include "db_chats.h"
#include "decision.h"
#include "http.h"
#include "main_.h"
#include "max_tokens.h"
#include "promptcache.h"
#include "utils.h"

#include "arg.h"
#include "log.h"

#include <filesystem>
#include <initializer_list>
#include <iostream>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <system_error>
#include <utility>

#include "ic_api.h"

/* ---------------------------------------------------------
  Wrapper around the main function of llama.cpp
  (-) Get the command arguments as a string
  (-) Parse the command arguments (string) into argc and argv
  (-) Call main_
  (-) Return output wrapped in a variant
*/

static void print_usage(int argc, char **argv) {
  // do nothing function
}

// A decision model has no text generation: it is served by run_decision.
static bool reject_if_decision_model(IC_API &ic_api) {
  if (!decision_model_loaded()) return false;
  send_output_record_result_error_to_wire(
      ic_api, Http::StatusCode::BadRequest,
      "The loaded model is a decision model: use run_decision.");
  return true;
}

// Starts a new chat: a new chat file for the caller, and the prompt cache
// checked (a stale one is discarded) with its UTF-8 carry cleared.
void new_chat() {
  IC_API ic_api(CanisterUpdate{std::string(__func__)}, false);
  std::string error_msg;
  if (!has_admin_update_or_whitelisted(ic_api)) {
    send_access_denied_output_record(ic_api);
    return;
  }
  if (reject_if_decision_model(ic_api)) return;

  CandidTypePrincipal caller = ic_api.get_caller();
  std::string principal_id = caller.get_text();

  // -----------------------------------------------------------
  // Create a new file to save this chat for this principal
  if (is_db_chats_active() && !db_chats_new(principal_id, error_msg)) {
    send_output_record_result_error_to_wire(
        ic_api, Http::StatusCode::InternalServerError, error_msg);
    return;
  }

  // Each principal can only save N chats
  if (is_db_chats_active() && !db_chats_clean(principal_id, error_msg)) {
    send_output_record_result_error_to_wire(
        ic_api, Http::StatusCode::InternalServerError, error_msg);
    return;
  }

  // -----------------------------------------------------------
  // Parse the arguments
  auto [argc, argv, args] = get_args_for_main(ic_api);

  // (-) gets the cache filename from --prompt-cache in args
  // (-) opens log file from --log-file in args
  common_params params;
  if (!common_params_parse(argc, argv.data(), params, LLAMA_EXAMPLE_COMPLETION,
                           print_usage)) {
    error_msg = "Cannot parse args.";
    send_output_record_result_error_to_wire(
        ic_api, Http::StatusCode::InternalServerError, error_msg);
    return;
  }

  // -----------------------------------------------------------
  // Each principal has their own cache folder
  std::string path_session = params.path_prompt_cache;
  std::string canister_path_session;
  if (!get_canister_path_session(path_session, principal_id,
                                 canister_path_session, error_msg)) {
    send_output_record_result_error_to_wire(
        ic_api, Http::StatusCode::InternalServerError, error_msg);
    return;
  }
  path_session = canister_path_session;

  // A new chat would reset what a running run_update_full carries over.
  if (session_busy(utf8_session_key(params.path_prompt_cache, principal_id))) {
    send_output_record_result_error_to_wire(ic_api, Http::StatusCode::Conflict,
                                            SESSION_BUSY_ERROR);
    return;
  }

  // A new chat starts from a clean slate: drop any half-codepoint left over from
  // a previous conversation on this session. Key it exactly as run() does -
  // path_session here is the CANONICAL path, which is a different string.
  utf8_carry_clear(utf8_session_key(params.path_prompt_cache, principal_id));

  std::string msg;
  if (!path_session.empty()) {

    // A prompt-cache written by an older llama.cpp passes llama.cpp's own
    // magic+version check (b4531 and b10076 are both 'ggsn' v9) but its
    // KV-cache layout changed, so it would be misparsed silently. Discard it.
    std::string stale_msg;
    if (prompt_cache_discard_if_stale(path_session, stale_msg)) {
      std::cout << "llama_cpp: " << std::string(__func__) << " - " << stale_msg
                << std::endl;
    }

    if (std::filesystem::exists(path_session)) {
      msg = "Re-using existing prompt-cache file " + path_session;
    } else {
      msg = "Will create new prompt-cache file " + path_session;
      // Stamp now so the file llama.cpp is about to create is recognized as
      // written by this build.
      prompt_cache_write_format_stamp(path_session);
    }
  } else {
    error_msg = "ERROR: path_session is empty ";
    send_output_record_result_error_to_wire(
        ic_api, Http::StatusCode::InternalServerError, error_msg);
    return;
  }
  std::cout << "llama_cpp: " << std::string(__func__) << " - " << msg
            << std::endl;

  // Simpler message back to the wire
  msg = "Ready to start a new chat for cache file " + path_session;

  // -----------------------------------------------------------
  // Return output over the wire
  CandidTypeRecord r_out;
  r_out.append("status_code", CandidTypeNat16{Http::StatusCode::OK}); // 200
  r_out.append("conversation", CandidTypeText{""});
  r_out.append("output", CandidTypeText{msg});
  r_out.append("error", CandidTypeText{""});
  r_out.append("prompt_remaining", CandidTypeText{""});
  r_out.append("generated_eog", CandidTypeBool{false});
  ic_api.to_wire(CandidTypeVariant{"Ok", r_out});
}

// A run call's request, read from the wire and parsed. argv points into args.
struct RunRequest {
  int argc = 0;
  std::vector<char *> argv;
  std::unique_ptr<std::vector<std::string>> args;
  common_params params;
  std::string principal_id;
};

// Authorizes the caller and reads and parses the args. Returns false after
// sending the error reply.
static bool run_request_from_wire(IC_API &ic_api, bool is_query,
                                  RunRequest &req) {
  bool authorized = is_query ? has_admin_query_or_whitelisted(ic_api)
                             : has_admin_update_or_whitelisted(ic_api);
  if (!authorized) {
    send_access_denied_output_record(ic_api);
    return false;
  }
  if (reject_if_decision_model(ic_api)) return false;

  CandidTypePrincipal caller = ic_api.get_caller();
  req.principal_id = caller.get_text();

  // Get the data from the wire and prepare arguments for main_
  auto [argc, argv, args] = get_args_for_main(ic_api);
  req.argc = argc;
  req.argv = std::move(argv);
  req.args = std::move(args);

  if (!common_params_parse(req.argc, req.argv.data(), req.params,
                           LLAMA_EXAMPLE_COMPLETION, print_usage)) {
    send_output_record_result_error_to_wire(
        ic_api, Http::StatusCode::InternalServerError, "Cannot parse args.");
    return false;
  }
  return true;
}

// The outcome of one main_() call: raw (unsanitized) text and token counts.
struct RunChunk {
  int result = 0;
  std::string icpp_error_msg;
  std::string conversation;     // input tokens (from session cache + prompt)
  std::string output;           // output tokens (generated during this call)
  std::string prompt_remaining; // part of the prompt not processed
  bool generated_eog = false;   // generated an eog (End Of Generation)
  // Exact token accounting for this call, put on the wire (opt nat64) on the
  // success record.
  uint64_t n_prompt_tokens = 0;
  uint64_t n_prompt_tokens_cached = 0;
  uint64_t n_prompt_tokens_decoded = 0;
  uint64_t n_tokens_generated = 0;
  uint64_t n_prompt_tokens_remaining = 0;
  bool n_predict_reached = false; // -n ended the call
};

// A run_update call stops decoding before it would use more instructions than
// this, keeping a margin to the IC's limit of 40B for one update message.
const uint64_t RUN_UPDATE_INSTRUCTIONS = 36'000'000'000ULL;
// The same after a yield of run_update_full: a reply callback gets 38B.
const uint64_t RUN_CALLBACK_INSTRUCTIONS = 34'000'000'000ULL;
// A runaway guard: a run_update_full never takes more chunks than this.
const uint32_t FULL_MAX_CHUNKS = 500;

// The sessions with a run_update_full in flight: which call holds each, and
// when it last made progress. Two generations on one session file overwrite
// each other's progress (and never finish), so another call on it is refused.
// An entry is removed when its generation ends. A trap in the first message
// rolls the entry back with it, but a trap after a yield leaves it: the
// cleanup destroys the coroutine without running its destructors (icpp-pro's
// ic_async.h). Such an entry expires when its call made no progress for
// FULL_BUSY_TTL_NS. A live call makes progress at least every ~5.5 minutes:
// a chunk takes seconds, and the yield is a bounded-wait self-call that the
// IC answers within 300s.
const uint64_t FULL_BUSY_TTL_NS = 10ULL * 60 * 1'000'000'000ULL;
struct BusyEntry {
  uint64_t owner;
  uint64_t seen_ns;
};
static std::map<std::string, BusyEntry> g_full_busy;
static uint64_t g_full_busy_owner = 0;

// Removes the entries whose call made no progress for FULL_BUSY_TTL_NS.
static void expire_busy() {
  const uint64_t now = IC_API::time();
  std::erase_if(g_full_busy, [now](const auto &kv) {
    return now - kv.second.seen_ns >= FULL_BUSY_TTL_NS;
  });
}

bool session_busy(const std::string &key) {
  expire_busy();
  return g_full_busy.contains(key);
}

bool full_call_running() {
  expire_busy();
  return !g_full_busy.empty();
}

// Holds the session `key` while it is in scope.
struct SessionBusy {
  std::string key;
  uint64_t owner;
  explicit SessionBusy(const std::string &k)
      : key(k), owner(++g_full_busy_owner) {
    g_full_busy[key] = {owner, IC_API::time()};
  }
  // Marks progress, so a call that is still running does not expire. False
  // when the entry expired, and this call no longer holds `key`.
  bool touch() {
    auto it = g_full_busy.find(key);
    if (it == g_full_busy.end() || it->second.owner != owner) return false;
    it->second.seen_ns = IC_API::time();
    return true;
  }
  // Releases the session, unless it expired and another call holds it now.
  ~SessionBusy() {
    auto it = g_full_busy.find(key);
    if (it != g_full_busy.end() && it->second.owner == owner)
      g_full_busy.erase(it);
  }
};

// Calls main_, just like it is called in the llama-cli app. max_instructions
// > 0 stops decoding before that many instructions (see main_).
static RunChunk run_chunk(int argc, char **argv,
                          const std::string &principal_id, uint64_t max_tokens,
                          uint64_t max_instructions) {
  RunChunk c;
  std::ostringstream conversation_ss;
  std::ostringstream output_ss;
  bool load_model_only = false;
  c.result = main_(argc, argv, principal_id, load_model_only, c.icpp_error_msg,
                   conversation_ss, output_ss, max_tokens, c.prompt_remaining,
                   c.generated_eog, c.n_prompt_tokens, c.n_prompt_tokens_cached,
                   c.n_prompt_tokens_decoded, c.n_tokens_generated,
                   c.n_prompt_tokens_remaining, max_instructions,
                   &c.n_predict_reached);
  c.conversation = conversation_ss.str();
  c.output = output_ss.str();
  return c;
}

// The Err reply of a failed run. The text arguments are valid UTF-8.
static void send_run_err(IC_API &ic_api, const std::string &conversation_text,
                         const std::string &output_text,
                         const std::string &error_text,
                         const std::string &prompt_remaining_text,
                         bool generated_eog) {
  CandidTypeRecord r_out;
  r_out.append("status_code",
               CandidTypeNat16{Http::StatusCode::InternalServerError}); // 500
  r_out.append("conversation", CandidTypeText{conversation_text});
  r_out.append("output", CandidTypeText{output_text});
  r_out.append("error", CandidTypeText{error_text});
  r_out.append("prompt_remaining", CandidTypeText{prompt_remaining_text});
  r_out.append("generated_eog", CandidTypeBool{generated_eog});
  ic_api.to_wire(CandidTypeVariant{"Err", r_out});
}

// The Ok reply of a run, with the token counts of `counts`. The text arguments
// are valid UTF-8.
static void send_run_ok(IC_API &ic_api, const std::string &conversation_text,
                        const std::string &output_text,
                        const std::string &prompt_remaining_text,
                        bool generated_eog, const RunChunk &counts,
                        std::optional<bool> partial = std::nullopt) {
  CandidTypeRecord r_out;
  r_out.append("status_code", CandidTypeNat16{Http::StatusCode::OK}); // 200
  r_out.append("conversation", CandidTypeText{conversation_text});
  r_out.append("output", CandidTypeText{output_text});
  r_out.append("error", CandidTypeText{""});
  r_out.append("prompt_remaining", CandidTypeText{prompt_remaining_text});
  r_out.append("generated_eog", CandidTypeBool{generated_eog});
  // Exact token accounting for this call (opt nat64). Only this success record
  // carries them; every other OutputRecordResult builder (errors, new_chat,
  // load_model, logs, promptcache) omits them, which is a valid candid subtype
  // so clients decode those as null (no run = no token counts).
  r_out.append("n_prompt_tokens", CandidTypeOptNat64{std::optional<uint64_t>{
                                      counts.n_prompt_tokens}});
  r_out.append("n_prompt_tokens_cached",
               CandidTypeOptNat64{
                   std::optional<uint64_t>{counts.n_prompt_tokens_cached}});
  r_out.append("n_prompt_tokens_decoded",
               CandidTypeOptNat64{
                   std::optional<uint64_t>{counts.n_prompt_tokens_decoded}});
  r_out.append("n_tokens_generated", CandidTypeOptNat64{std::optional<uint64_t>{
                                         counts.n_tokens_generated}});
  r_out.append("n_prompt_tokens_remaining",
               CandidTypeOptNat64{
                   std::optional<uint64_t>{counts.n_prompt_tokens_remaining}});
  // run_update_full only: whether the response was cut short
  if (partial.has_value()) {
    r_out.append("partial", CandidTypeOptBool{partial});
  }
  ic_api.to_wire(CandidTypeVariant{"Ok", r_out});
}

// ---------------------------------------------------------------------------
// Make every outgoing Candid `text` valid UTF-8 (see utils.h).
//
// `output` is CARRIED, not sanitized: callers concatenate the output of
// successive run_update calls, so a codepoint split across a chunk boundary
// must be DEFERRED to the next call, never dropped or replaced.
//
// `conversation` and `prompt_remaining` are rebuilt from the token vector on
// every call rather than accumulated, so there is nothing to carry - they are
// sanitized. That is lossy but safe: callers treat them as informational and
// re-send the FULL prompt until prompt_remaining is empty (see the README run
// loop), so a U+FFFD here is never fed back in as input.
//
// The carry needs a per-session, per-principal key (session_key). Derive it with
// pure string work (utf8_session_key): get_canister_path_session() would do the
// same normalization, but it also stats the filesystem and may create
// directories - a VFS round trip, on every single update call, against a
// canister whose stable memory holds the whole prompt-cache tree. The key never
// needs the path to exist.
//
// An empty key is NOT usable: without the emptiness check every principal
// calling without a --prompt-cache would share one carry entry, and one
// caller's trailing bytes could be prepended to another caller's output.
//
// Returns the output to reply with. can_carry: false for a query, whose state
// changes are discarded. flush: the conversation finished (or failed), so no
// bytes are kept for a next call.
static std::string carry_output(const std::string &session_key, bool can_carry,
                                std::string output_text, bool flush) {
  if (can_carry && !session_key.empty()) {
    std::string full = utf8_carry_get(session_key) + output_text;
    // ONE pass yields both the split point and whether the prefix holds an
    // invalid byte, so the common case (valid text) never walks it twice nor
    // builds a second buffer.
    bool clean = true;
    const size_t n = utf8_valid_prefix_len(full, &clean);
    std::string tail = full.substr(n); // 0-3 bytes of a split codepoint
    full.resize(n);                    // truncate in place; no copy
    output_text = clean ? std::move(full) : utf8_sanitize(std::move(full));
    if (flush) {
      // Flush: never retain bytes across a finished conversation.
      output_text += utf8_sanitize(std::move(tail));
      utf8_carry_clear(session_key);
    } else {
      utf8_carry_set(session_key, tail);
    }
    return output_text;
  }
  // A query call cannot carry: state changes are discarded when the message
  // ends. Sanitize instead, accepting the loss.
  return utf8_sanitize(std::move(output_text));
}

// One run_query or run_update call: read the request, run one chunk of at most
// max_tokens, and reply.
void run(IC_API &ic_api, const uint64_t &max_tokens, bool is_query) {
  RunRequest req;
  if (!run_request_from_wire(ic_api, is_query, req)) return;

  if (!is_query && session_busy(utf8_session_key(req.params.path_prompt_cache,
                                                 req.principal_id))) {
    send_output_record_result_error_to_wire(ic_api, Http::StatusCode::Conflict,
                                            SESSION_BUSY_ERROR);
    return;
  }

  // If we're going to load a new model, first free the Orthogonally Persisted memory of a previously loaded model
  if (!req.params.model.empty()) {
    if (full_call_running()) {
      send_output_record_result_error_to_wire(
          ic_api, Http::StatusCode::Conflict,
          "a run_update_full is running: cannot change the model.");
      return;
    }
    icpp_free_model();
  }

  const RunChunk c =
      run_chunk(req.argc, req.argv.data(), req.principal_id, max_tokens,
                is_query ? 0 : RUN_UPDATE_INSTRUCTIONS);

  // Make every outgoing Candid `text` valid UTF-8 (see carry_output)
  const std::string session_key =
      utf8_session_key(req.params.path_prompt_cache, req.principal_id);
  const std::string output_text = carry_output(
      session_key, !is_query, c.output, c.generated_eog || c.result != 0);
  const std::string conversation_text = utf8_sanitize(c.conversation);
  const std::string prompt_remaining_text = utf8_sanitize(c.prompt_remaining);

  // Exit if there was an error
  if (c.result != 0) {
    send_run_err(ic_api, conversation_text, output_text,
                 utf8_sanitize(c.icpp_error_msg), prompt_remaining_text,
                 c.generated_eog);
    return;
  }

  // Append output to latest chat file for this prinicipal
  std::string icpp_error_msg;
  if (is_db_chats_active() &&
      !db_chats_save_conversation(conversation_text, req.principal_id,
                                  icpp_error_msg)) {
    send_output_record_result_error_to_wire(
        ic_api, Http::StatusCode::InternalServerError, icpp_error_msg);
    return;
  }

  // Return output over the wire
  send_run_ok(ic_api, conversation_text, output_text, prompt_remaining_text,
              c.generated_eog, c);
}

// run as a query: max_tokens_query tokens, no state kept.
void run_query() {
  IC_API ic_api(CanisterQuery{std::string(__func__)}, false);
  run(ic_api, max_tokens_query, true);
}
// run as an update: max_tokens_update tokens, and the state it changes is kept.
void run_update() {
  IC_API ic_api(CanisterUpdate{std::string(__func__)}, false);
  run(ic_api, max_tokens_update, false);
}

// ---------------------------------------------------------------------------
// run_update_full: ONE update call that ingests the prompt and generates the
// whole response, in chunks. After each chunk the coroutine yields to itself
// (yield_fresh_budget), which commits that chunk - the session file included -
// and continues in a new message with a fresh instruction limit.
//
// Other messages can run between two chunks and change the shared model
// context, so every chunk starts from the caller's session file: main_()
// always clears the context and reloads that file.

// The index of the value of the first option in `names`, or -1.
static int find_arg_value(const std::vector<std::string> &args,
                          std::initializer_list<const char *> names) {
  for (size_t i = 0; i + 1 < args.size(); ++i) {
    for (const char *name : names) {
      if (args[i] == name) return (int)i + 1;
    }
  }
  return -1;
}

// Sets the value at index i of the args, and its argv pointer.
static void set_arg_value(RunRequest &req, int i, const std::string &value) {
  (*req.args)[i] = value;
  req.argv[i] = (*req.args)[i].data();
}

// The generation loop of run_update_full. A coroutine: each co_await is a new
// message.
static IC_Async run_update_full_async() {
  IC_API ic_api(CanisterUpdate{"run_update_full"}, false);
  RunRequest req;
  if (!run_request_from_wire(ic_api, false, req)) co_return;

  if (!req.params.model.empty()) {
    send_output_record_result_error_to_wire(
        ic_api, Http::StatusCode::BadRequest,
        "run_update_full does not load a model: use load_model.");
    co_return;
  }
  const std::string session_key =
      utf8_session_key(req.params.path_prompt_cache, req.principal_id);
  if (session_key.empty()) {
    send_output_record_result_error_to_wire(
        ic_api, Http::StatusCode::BadRequest,
        "run_update_full needs a --prompt-cache.");
    co_return;
  }
  if (session_busy(session_key)) {
    send_output_record_result_error_to_wire(ic_api, Http::StatusCode::Conflict,
                                            SESSION_BUSY_ERROR);
    co_return;
  }
  SessionBusy busy{session_key};

  // The chat to save this conversation in: the latest one now. A new_chat
  // while this call is suspended starts another chat, which must not get it.
  std::string chat_file;
  if (is_db_chats_active()) {
    std::string chat_error;
    if (!db_chats_latest_file(req.principal_id, chat_file, chat_error)) {
      send_output_record_result_error_to_wire(
          ic_api, Http::StatusCode::InternalServerError, chat_error);
      co_return;
    }
  }

  // -n is the total for the whole response; each chunk gets what is left.
  const int64_t n_total = req.params.n_predict; // < 0: until eog
  const int i_prompt = find_arg_value(*req.args, {"-p", "--prompt"});
  const int i_n = find_arg_value(*req.args, {"-n", "--n-predict", "--predict"});

  std::string output; // raw bytes; made valid UTF-8 once, at the end
  RunChunk totals;    // the token counts of the whole call
  RunChunk c;
  uint32_t chunks = 0;
  bool partial = false;
  uint64_t max_instructions = RUN_UPDATE_INSTRUCTIONS;
  while (true) {
    const uint64_t ins_0 = instruction_counter();
    c = run_chunk(req.argc, req.argv.data(), req.principal_id,
                  max_tokens_update, max_instructions);
    ++chunks;
    if (chunks == 1) {
      totals.n_prompt_tokens = c.n_prompt_tokens;
      totals.n_prompt_tokens_cached = c.n_prompt_tokens_cached;
    }
    output += c.output;
    totals.n_prompt_tokens_decoded += c.n_prompt_tokens_decoded;
    totals.n_tokens_generated += c.n_tokens_generated;
    totals.n_prompt_tokens_remaining = c.n_prompt_tokens_remaining;
    std::cout << "llama_cpp: run_update_full - chunk " << chunks
              << ": decoded=" << c.n_prompt_tokens_decoded
              << " generated=" << c.n_tokens_generated
              << " prompt_remaining_tokens=" << c.n_prompt_tokens_remaining
              << " eog=" << c.generated_eog
              << " instructions=" << instruction_counter() - ins_0 << std::endl;
    if (c.result != 0) break;
    if (c.generated_eog || c.n_predict_reached) break;
    if (c.n_prompt_tokens_decoded == 0 && c.n_tokens_generated == 0) {
      c.result = 1;
      c.icpp_error_msg = "run_update_full: a chunk made no progress";
      break;
    }
    if (chunks >= FULL_MAX_CHUNKS) {
      partial = true;
      break;
    }

    // Once the prompt is in the session file, continue from it.
    if (c.n_prompt_tokens_remaining == 0 && i_prompt > 0) {
      set_arg_value(req, i_prompt, "");
    }
    if (n_total >= 0 && i_n > 0) {
      set_arg_value(
          req, i_n,
          std::to_string(n_total - (int64_t)totals.n_tokens_generated));
    }

    // Commit this chunk and continue in a new message. On an error (the
    // canister is stopping, or the yield was not performed), reply with what
    // this call has.
    IC_CallResult<void> r = co_await ic_api.yield_fresh_budget();
    if (!r) {
      std::cout << "llama_cpp: run_update_full - yield failed: "
                << r.error().to_string() << std::endl;
      partial = true;
      break;
    }
    max_instructions = RUN_CALLBACK_INSTRUCTIONS;
    // The session expired while this call waited, and another call may hold
    // it now: leave its files alone.
    if (!busy.touch()) {
      std::cout << "llama_cpp: run_update_full - lost its session after "
                << chunks << " chunks" << std::endl;
      send_output_record_result_error_to_wire(
          ic_api, Http::StatusCode::Conflict,
          "run_update_full: the session expired while this call waited, and "
          "is no longer held by it.");
      co_return;
    }
  }
  // The token that ended the call by -n was sampled, not decoded.
  if (c.n_predict_reached) ++totals.n_tokens_generated;
  std::cout << "llama_cpp: run_update_full - done: chunks=" << chunks
            << " generated=" << totals.n_tokens_generated
            << " partial=" << partial << std::endl;

  const std::string output_text =
      carry_output(session_key, true, output, c.generated_eog || c.result != 0);
  const std::string conversation_text = utf8_sanitize(c.conversation);
  const std::string prompt_remaining_text = utf8_sanitize(c.prompt_remaining);
  if (c.result != 0) {
    send_run_err(ic_api, conversation_text, output_text,
                 utf8_sanitize(c.icpp_error_msg), prompt_remaining_text,
                 c.generated_eog);
    co_return;
  }

  std::string icpp_error_msg;
  if (is_db_chats_active() && !chat_file.empty() &&
      !db_chats_write_conversation(chat_file, conversation_text,
                                   req.principal_id, icpp_error_msg)) {
    send_output_record_result_error_to_wire(
        ic_api, Http::StatusCode::InternalServerError, icpp_error_msg);
    co_return;
  }

  send_run_ok(ic_api, conversation_text, output_text, prompt_remaining_text,
              c.generated_eog, totals, partial);
}

// One update call that ingests the prompt and generates the whole response.
void run_update_full() { run_update_full_async(); }
