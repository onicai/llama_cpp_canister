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
#include <iostream>
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
                        bool generated_eog, const RunChunk &counts) {
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
  ic_api.to_wire(CandidTypeVariant{"Ok", r_out});
}

// One run_query or run_update call: read the request, run one chunk of at most
// max_tokens, and reply.
void run(IC_API &ic_api, const uint64_t &max_tokens, bool is_query) {
  RunRequest req;
  if (!run_request_from_wire(ic_api, is_query, req)) return;

  // If we're going to load a new model, first free the Orthogonally Persisted memory of a previously loaded model
  if (!req.params.model.empty()) {
    icpp_free_model();
  }

  const RunChunk c =
      run_chunk(req.argc, req.argv.data(), req.principal_id, max_tokens,
                is_query ? 0 : RUN_UPDATE_INSTRUCTIONS);

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
  std::string output_text = c.output;
  // The carry needs a per-session, per-principal key. Derive it with pure string
  // work: get_canister_path_session() would do the same normalization, but it
  // also stats the filesystem and may create directories - a VFS round trip, on
  // every single update call, against a canister whose stable memory holds the
  // whole prompt-cache tree. The key never needs the path to exist.
  //
  // An empty key is NOT usable: without the emptiness check every principal
  // calling without a --prompt-cache would share one carry entry, and one
  // caller's trailing bytes could be prepended to another caller's output.
  const std::string session_key =
      utf8_session_key(req.params.path_prompt_cache, req.principal_id);
  const bool have_session_key = !is_query && !session_key.empty();
  if (have_session_key) {
    std::string full = utf8_carry_get(session_key) + output_text;
    // ONE pass yields both the split point and whether the prefix holds an
    // invalid byte, so the common case (valid text) never walks it twice nor
    // builds a second buffer.
    bool clean = true;
    const size_t n = utf8_valid_prefix_len(full, &clean);
    std::string tail = full.substr(n); // 0-3 bytes of a split codepoint
    full.resize(n);                    // truncate in place; no copy
    output_text = clean ? std::move(full) : utf8_sanitize(std::move(full));
    if (c.generated_eog || c.result != 0) {
      // Flush: never retain bytes across a finished conversation.
      output_text += utf8_sanitize(std::move(tail));
      utf8_carry_clear(session_key);
    } else {
      utf8_carry_set(session_key, tail);
    }
  } else {
    // A query call cannot carry: state changes are discarded when the message
    // ends. Sanitize instead, accepting the loss.
    output_text = utf8_sanitize(std::move(output_text));
  }
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
