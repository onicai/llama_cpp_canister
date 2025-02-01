#include "run.h"
#include "auth.h"
#include "common.h"
#include "db_chats.h"
#include "http.h"
#include "main_.h"
#include "max_tokens.h"
#include "utils.h"

#include "arg.h"
#include "log.h"

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

static void print_usage(int argc, char **argv) {
  // do nothing function
}

void new_chat() {
  IC_API ic_api(CanisterUpdate{std::string(__func__)}, false);
  std::string error_msg;
  if (!is_caller_whitelisted(ic_api, false)) {
    error_msg = "Access Denied.";
    send_output_record_result_error_to_wire(
        ic_api, Http::StatusCode::Unauthorized, error_msg);
    return;
  }

  CandidTypePrincipal caller = ic_api.get_caller();
  std::string principal_id = caller.get_text();

  // -----------------------------------------------------------
  // Create a new file to save this chat for this prinicipal
  if (!db_chats_new(principal_id, error_msg)) {
    send_output_record_result_error_to_wire(
        ic_api, Http::StatusCode::InternalServerError, error_msg);
    return;
  }

  // Each principal can only save N chats
  if (!db_chats_clean(principal_id, error_msg)) {
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
  if (!common_params_parse(argc, argv.data(), params, LLAMA_EXAMPLE_MAIN,
                           print_usage)) {
    error_msg = "Cannot parse args.";
    send_output_record_result_error_to_wire(
        ic_api, Http::StatusCode::InternalServerError, error_msg);
    return;
  }

  // -----------------------------------------------------------
  // Create/reset a prompt-cache file to zero length, will reset the LLM state for that conversation
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

  std::string msg;
  if (!path_session.empty()) {

    if (std::filesystem::exists(path_session)) {
      // First, remove the file if it exists
      bool success = std::filesystem::remove(path_session);
      if (success) {
        msg = "Cache file " + path_session + " deleted successfully";
      } else {
        error_msg = "Error deleting cache file " + path_session;
        send_output_record_result_error_to_wire(
            ic_api, Http::StatusCode::InternalServerError, error_msg);
        return;
      }
    } else {
      msg = "Cache file " + path_session + " not found. Nothing to delete.";
    }
  } else {
    error_msg = "ERROR: path_session is empty ";
    send_output_record_result_error_to_wire(
        ic_api, Http::StatusCode::InternalServerError, error_msg);
    return;
  }
  std::cout << msg << std::endl;

  // Simpler message back to the wire
  msg = "Ready to start a new chat for cache file " + path_session;

  // -----------------------------------------------------------
  // If --log-file is provided, the file was opened by common_params_parse
  // Was it already closed, and common_log_main() does not work anymore???
  // If so, then store --log-file value in params.log_file, and delete it here
  // If not, then get the file handle from common_log_main() and empty the file
  //
  // When running native, the log file is only closed at the end...
  //                                   it is opened multiple times. Does that work OK ?

  // When running in the IC, the log file is ????

  std::cout << "TODO";

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

void remove_prompt_cache() {
  IC_API ic_api(CanisterUpdate{std::string(__func__)}, false);
  std::string error_msg;
  if (!is_caller_whitelisted(ic_api, false)) {
    error_msg = "Access Denied.";
    send_output_record_result_error_to_wire(
        ic_api, Http::StatusCode::Unauthorized, error_msg);
    return;
  }

  CandidTypePrincipal caller = ic_api.get_caller();
  std::string principal_id = caller.get_text();

  auto [argc, argv, args] = get_args_for_main(ic_api);

  // Get the cache filename from --prompt-cache in args
  common_params params;
  if (!common_params_parse(argc, argv.data(), params, LLAMA_EXAMPLE_MAIN,
                           print_usage)) {
    error_msg = "Cannot parse args.";
    send_output_record_result_error_to_wire(
        ic_api, Http::StatusCode::InternalServerError, error_msg);
    return;
  }

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

  std::string msg;
  if (!path_session.empty()) {
    // Remove the file if it exists
    if (std::filesystem::exists(path_session)) {
      bool success = std::filesystem::remove(path_session);
      if (success) {
        msg = "Cache file " + path_session + " deleted successfully";
      } else {
        error_msg = "Error deleting cache file " + path_session;
        send_output_record_result_error_to_wire(
            ic_api, Http::StatusCode::InternalServerError, error_msg);
        return;
      }
    } else {
      msg = "Cache file " + path_session + " not found. Nothing to delete.";
    }
  } else {
    error_msg = "ERROR: path_session is empty ";
    send_output_record_result_error_to_wire(
        ic_api, Http::StatusCode::InternalServerError, error_msg);
    return;
  }
  std::cout << msg << std::endl;

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

void remove_log_file() {
  IC_API ic_api(CanisterUpdate{std::string(__func__)}, false);
  std::string error_msg;
  if (!is_caller_whitelisted(ic_api, false)) {
    error_msg = "Access Denied.";
    send_output_record_result_error_to_wire(
        ic_api, Http::StatusCode::Unauthorized, error_msg);
    return;
  }

  auto [argc, argv, args] = get_args_for_main(ic_api);

  // Process the args, which will instantiate the log singleton
  common_params params;
  if (!common_params_parse(argc, argv.data(), params, LLAMA_EXAMPLE_MAIN,
                           print_usage)) {
    error_msg = "Cannot parse args.";
    send_output_record_result_error_to_wire(
        ic_api, Http::StatusCode::InternalServerError, error_msg);
    return;
  }

  // Now we can remove the log file
  std::string msg;
  bool success = common_log_remove_file(common_log_main(), msg);
  if (!success) {
    send_output_record_result_error_to_wire(
        ic_api, Http::StatusCode::InternalServerError, msg);
    return;
  }

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
  std::string error_msg;
  if (!is_caller_whitelisted(ic_api, false)) {
    error_msg = "Access Denied.";
    send_output_record_result_error_to_wire(
        ic_api, Http::StatusCode::Unauthorized, error_msg);
    return;
  }

  CandidTypePrincipal caller = ic_api.get_caller();
  std::string principal_id = caller.get_text();

  // Get the data from the wire and prepare arguments for main_
  auto [argc, argv, args] = get_args_for_main(ic_api);

  common_params params;
  if (!common_params_parse(argc, argv.data(), params, LLAMA_EXAMPLE_MAIN,
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
    send_output_record_result_error_to_wire(
        ic_api, Http::StatusCode::InternalServerError, icpp_error_msg);
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
