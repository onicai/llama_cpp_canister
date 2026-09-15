#include "run.h"
#include "auth.h"
#include "common.h"
#include "db_chats.h"
#include "http.h"
#include "main_.h"
#include "max_tokens.h"
#include "promptcache.h"
#include "utils.h"

#include "arg.h"
#include "log.h"

#include <filesystem>
#include <iostream>
#include <optional>
#include <string>
#include <system_error>

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

void new_chat() {
  IC_API ic_api(CanisterUpdate{std::string(__func__)}, false);
  std::string error_msg;
  if (!has_admin_update_or_whitelisted(ic_api)) {
    send_access_denied_output_record(ic_api);
    return;
  }

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
  // a previous conversation on this session.
  utf8_carry_clear(path_session);

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

void run(IC_API &ic_api, const uint64_t &max_tokens, bool is_query) {
  std::string error_msg;
  bool authorized = is_query ? has_admin_query_or_whitelisted(ic_api)
                             : has_admin_update_or_whitelisted(ic_api);
  if (!authorized) {
    send_access_denied_output_record(ic_api);
    return;
  }

  CandidTypePrincipal caller = ic_api.get_caller();
  std::string principal_id = caller.get_text();

  // Get the data from the wire and prepare arguments for main_
  auto [argc, argv, args] = get_args_for_main(ic_api);

  common_params params;
  if (!common_params_parse(argc, argv.data(), params, LLAMA_EXAMPLE_COMPLETION,
                           print_usage)) {
    error_msg = "Cannot parse args.";
    send_output_record_result_error_to_wire(
        ic_api, Http::StatusCode::InternalServerError, error_msg);
    return;
  }

  // If we're going to load a new model, first free the Orthogonally Persisted memory of a previously loaded model
  if (!params.model.empty()) {
    icpp_free_model();
  }

  // Call main_, just like it is called in the llama-cli app
  std::string icpp_error_msg;
  std::ostringstream
      conversation_ss;          // input tokens (from session cache + prompt)
  std::ostringstream output_ss; // output tokens (generated during this call)
  std::string
      prompt_remaining; // part of the prompt not processed due to max_tokens
  bool generated_eog =
      false; // this is set to true if llama.cpp is generating new tokens, and it generated an eog (End Of Generation)
  // Exact token accounting for this call, filled by main_() and put on the wire
  // (opt nat64) on the success record below.
  uint64_t n_prompt_tokens = 0;
  uint64_t n_prompt_tokens_cached = 0;
  uint64_t n_prompt_tokens_decoded = 0;
  uint64_t n_tokens_generated = 0;
  uint64_t n_prompt_tokens_remaining = 0;
  bool load_model_only = false;
  int result = main_(argc, argv.data(), principal_id, load_model_only,
                     icpp_error_msg, conversation_ss, output_ss, max_tokens,
                     prompt_remaining, generated_eog, n_prompt_tokens,
                     n_prompt_tokens_cached, n_prompt_tokens_decoded,
                     n_tokens_generated, n_prompt_tokens_remaining);

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
  std::string output_text = output_ss.str();
  std::string session_key;
  std::string session_key_err;
  // The carry needs a per-session, per-principal key. get_canister_path_session()
  // returns TRUE with an EMPTY string when no --prompt-cache was given, so the
  // emptiness check is load-bearing: without it every principal calling without a
  // prompt cache would share one carry entry, and one caller's trailing bytes
  // could be prepended to another caller's output.
  const bool have_session_key =
      !is_query &&
      get_canister_path_session(params.path_prompt_cache, principal_id,
                                session_key, session_key_err) &&
      !session_key.empty();
  if (have_session_key) {
    std::string full = utf8_carry_get(session_key) + output_text;
    const size_t n = utf8_valid_prefix_len(full);
    output_text = utf8_sanitize(full.substr(0, n));
    std::string tail = full.substr(n);
    if (generated_eog || result != 0) {
      // Flush: never retain bytes across a finished conversation.
      output_text += utf8_sanitize(tail);
      utf8_carry_clear(session_key);
    } else {
      utf8_carry_set(session_key, tail);
    }
  } else {
    // A query call cannot carry: state changes are discarded when the message
    // ends. Sanitize instead, accepting the loss.
    output_text = utf8_sanitize(output_text);
  }
  const std::string conversation_text = utf8_sanitize(conversation_ss.str());
  const std::string prompt_remaining_text = utf8_sanitize(prompt_remaining);

  // Exit if there was an error
  if (result != 0) {
    CandidTypeRecord r_out;
    r_out.append("status_code",
                 CandidTypeNat16{Http::StatusCode::InternalServerError}); // 500
    r_out.append("conversation", CandidTypeText{conversation_text});
    r_out.append("output", CandidTypeText{output_text});
    r_out.append("error", CandidTypeText{utf8_sanitize(icpp_error_msg)});
    r_out.append("prompt_remaining", CandidTypeText{prompt_remaining_text});
    r_out.append("generated_eog", CandidTypeBool{generated_eog});
    ic_api.to_wire(CandidTypeVariant{"Err", r_out});
    return;
  }

  // Append output to latest chat file for this prinicipal
  if (is_db_chats_active() &&
      !db_chats_save_conversation(conversation_text, principal_id,
                                  icpp_error_msg)) {
    send_output_record_result_error_to_wire(
        ic_api, Http::StatusCode::InternalServerError, icpp_error_msg);
    return;
  }

  // Return output over the wire
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
  r_out.append("n_prompt_tokens",
               CandidTypeOptNat64{std::optional<uint64_t>{n_prompt_tokens}});
  r_out.append(
      "n_prompt_tokens_cached",
      CandidTypeOptNat64{std::optional<uint64_t>{n_prompt_tokens_cached}});
  r_out.append(
      "n_prompt_tokens_decoded",
      CandidTypeOptNat64{std::optional<uint64_t>{n_prompt_tokens_decoded}});
  r_out.append("n_tokens_generated",
               CandidTypeOptNat64{std::optional<uint64_t>{n_tokens_generated}});
  r_out.append(
      "n_prompt_tokens_remaining",
      CandidTypeOptNat64{std::optional<uint64_t>{n_prompt_tokens_remaining}});
  ic_api.to_wire(CandidTypeVariant{"Ok", r_out});
}

void run_query() {
  IC_API ic_api(CanisterQuery{std::string(__func__)}, false);
  run(ic_api, max_tokens_query, true);
}
void run_update() {
  IC_API ic_api(CanisterUpdate{std::string(__func__)}, false);
  run(ic_api, max_tokens_update, false);
}
