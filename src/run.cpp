#include "run.h"
#include "common.h"
#include "db_chats.h"
#include "http.h"
#include "main_.h"
#include "max_tokens.h"
#include "utils.h"

#include <filesystem>
#include <iostream>
#include <string>
#include <system_error>

#include "ic_api.h"

/* ---------------------------------------------------------
  Wrapper around the main function of llama.cpp
  (-) Get the command arguments as a string
  (-) Parse the command arguments (string) into argc and argv
  (-) Call main_
  (-) Return output wrapped in a variant

  Two endpoints are provided:
  (-) run_query
  (-) run_update
*/

void new_chat() {
  IC_API ic_api(CanisterUpdate{std::string(__func__)}, false);
  CandidTypePrincipal caller = ic_api.get_caller();
  std::string principal_id = caller.get_text();

  auto [argc, argv, args] = get_args_for_main(ic_api);

  std::string error_msg;

  // Create/reset a prompt-cache file to zero length, will reset the LLM state for that conversation
  // Get the cache filename from --prompt-cache in args
  gpt_params params;
  if (!gpt_params_parse(argc, argv.data(), params)) {
    error_msg = "Cannot parse args.";
    send_output_record_result_error_to_wire(ic_api, error_msg);
    return;
  }

  // Create a new file to save this chat for this prinicipal
  if (!db_chats_new(principal_id, error_msg)) {
    send_output_record_result_error_to_wire(ic_api, error_msg);
    return;
  }

  // Each principal can only save N chats
  if (!db_chats_clean(principal_id, error_msg)) {
    send_output_record_result_error_to_wire(ic_api, error_msg);
    return;
  }

  // Each principal has their own cache folder
  std::string path_session = params.path_prompt_cache;
  std::string canister_path_session;
  if (!get_canister_path_session(path_session, principal_id,
                                 canister_path_session, error_msg)) {
    send_output_record_result_error_to_wire(ic_api, error_msg);
    return;
  }
  path_session = canister_path_session;

  std::string msg;
  if (!path_session.empty()) {

    if (std::filesystem::exists(path_session)) {
      // First, remove the file if it exists
      bool success = std::filesystem::remove(path_session);
      if (success) {
        msg = "Cache file " + path_session + " deleted successfully";
      } else {
        error_msg = "Error deleting cache file " + path_session;
        send_output_record_result_error_to_wire(ic_api, error_msg);
        return;
      }
    } else {
      msg = "Cache file " + path_session + " not found. Nothing to delete.";
    }
  }
  std::cout << msg << std::endl;

  // Simpler message back to the wire
  msg = "Ready to start a new chat for cache file " + path_session;

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

void run(IC_API &ic_api, const uint64_t &max_tokens) {
  CandidTypePrincipal caller = ic_api.get_caller();
  std::string principal_id = caller.get_text();

  // Get the data from the wire and prepare arguments for main_
  auto [argc, argv, args] = get_args_for_main(ic_api);

  // Call main_, just like it is called in the llama-cli app
  std::string icpp_error_msg;
  std::ostringstream
      conversation_ss;          // input tokens (from session cache + prompt)
  std::ostringstream output_ss; // output tokens (generated during this call)
  std::string
      prompt_remaining; // part of the prompt not processed due to max_tokens
  bool generated_eog =
      false; // this is set to true if llama.cpp is generating new tokens, and it generated an eog (End Of Generation)
  bool load_model_only = false;
  int result = main_(argc, argv.data(), principal_id, load_model_only,
                     icpp_error_msg, conversation_ss, output_ss, max_tokens,
                     prompt_remaining, generated_eog);

  // Exit if there was an error
  if (result != 0) {
    CandidTypeRecord r_out;
    r_out.append("status_code",
                 CandidTypeNat16{Http::StatusCode::InternalServerError}); // 500
    r_out.append("conversation", CandidTypeText{conversation_ss.str()});
    r_out.append("output", CandidTypeText{output_ss.str()});
    r_out.append("error", CandidTypeText{icpp_error_msg});
    r_out.append("prompt_remaining", CandidTypeText{prompt_remaining});
    r_out.append("generated_eog", CandidTypeBool{generated_eog});
    ic_api.to_wire(CandidTypeVariant{"Err", r_out});
    return;
  }

  // Append output to latest chat file for this prinicipal
  if (!db_chats_save_conversation(conversation_ss.str(), principal_id,
                                  icpp_error_msg)) {
    send_output_record_result_error_to_wire(ic_api, icpp_error_msg);
    return;
  }

  // Return output over the wire
  CandidTypeRecord r_out;
  r_out.append("status_code", CandidTypeNat16{Http::StatusCode::OK}); // 200
  r_out.append("conversation", CandidTypeText{conversation_ss.str()});
  r_out.append("output", CandidTypeText{output_ss.str()});
  r_out.append("error", CandidTypeText{""});
  r_out.append("prompt_remaining", CandidTypeText{prompt_remaining});
  r_out.append("generated_eog", CandidTypeBool{generated_eog});
  ic_api.to_wire(CandidTypeVariant{"Ok", r_out});
}

void run_query() {
  IC_API ic_api(CanisterQuery{std::string(__func__)}, false);
  run(ic_api, max_tokens_query);
}
void run_update() {
  IC_API ic_api(CanisterUpdate{std::string(__func__)}, false);
  run(ic_api, max_tokens_update);
}
