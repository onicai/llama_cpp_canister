#include "prompt_cache.h"

#include "auth.h"
#include "common.h"
#include "db_chats.h"
#include "http.h"
#include "main_.h"
#include "max_tokens.h"
#include "run.h"
#include "utils.h"

#include "arg.h"
#include "log.h"

#include <filesystem>
#include <iostream>
#include <string>
#include <system_error>

#include "ic_api.h"

/*
prompt-cache is also called a session file
It contains the state of the LLM and is preserved between update calls

This module provides functions to manage the prompt-cache file to
optimize performance & cost
*/

static void print_usage(int argc, char **argv) {
  // do nothing function
}

bool get_canister_path_session(const std::string &path_session,
                               const std::string &principal_id,
                               std::string &canister_path_session,
                               std::string &error_msg) {
  // We store the prompt-cache files in a folder named with the principal id of the caller
  //
  // Note: to save multiple conversations per user, the front end can simply assign
  //       a unique prompt-cache file per conversation, and that will do the job !
  //

  std::string path_session_ = path_session;
  canister_path_session = "";
  error_msg = "";

  if (!path_session_.empty()) {
    // Remove all leading '/'
    size_t pos = path_session_.find_first_not_of('/');
    if (pos != std::string::npos) {
      path_session_.erase(0, pos);
    } else {
      // If the string only contains slashes, clear it
      path_session_.clear();
    }

    // The cache file will be stored in ".cache/<principal_id>/<path_session-with_/replaced-by-_>"
    canister_path_session =
        ".canister_cache/" + principal_id + "/sessions/" + path_session_;

    // Make sure that the cache directory exists, else llama.cpp cannot create the file
    std::filesystem::path file_path(canister_path_session);
    std::filesystem::path dir_path = file_path.parent_path();
    if (!my_create_directory(dir_path, error_msg)) {
      return false;
    }
  }
  return true;
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
  // std::cout << "llama_cpp: " << std::string(__func__) << " - " << msg << std::endl;

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

void copy_prompt_cache() {
  IC_API ic_api(CanisterUpdate{std::string(__func__)}, false);
  std::string error_msg;
  if (!is_caller_whitelisted(ic_api, false)) {
    error_msg = "Access Denied";
    ic_api.to_wire(CandidTypeVariant{
        "Err", CandidTypeVariant{"Other", CandidTypeText{error_msg}}});
    return;
  }

  CandidTypePrincipal caller = ic_api.get_caller();
  std::string principal_id = caller.get_text();

  std::string from{""};
  std::string to{""};
  CandidTypeRecord r_in;
  r_in.append("from", CandidTypeText{&from});
  r_in.append("to", CandidTypeText{&to});
  ic_api.from_wire(r_in);

  // Each principal has their own cache folder
  std::string from_path;
  if (!get_canister_path_session(from, principal_id, from_path, error_msg)) {
    ic_api.to_wire(CandidTypeVariant{
        "Err", CandidTypeVariant{"Other", CandidTypeText{error_msg}}});
    return;
  }
  std::string to_path;
  if (!get_canister_path_session(to, principal_id, to_path, error_msg)) {
    ic_api.to_wire(CandidTypeVariant{
        "Err", CandidTypeVariant{"Other", CandidTypeText{error_msg}}});
    return;
  }

  // copy the file from_path to to_path if it exists
  if (!std::filesystem::exists(from_path)) {
    error_msg = "File " + from_path + " does not exist.";
    ic_api.to_wire(CandidTypeVariant{
        "Err", CandidTypeVariant{"Other", CandidTypeText{error_msg}}});
    return;
  }

  // first remove the 'to' file if it already exists
  if (std::filesystem::exists(to_path)) {
    bool success = std::filesystem::remove(to_path);
    if (!success) {
      error_msg = "Could not remove existing 'to' file " + to_path;
      ic_api.to_wire(CandidTypeVariant{
          "Err", CandidTypeVariant{"Other", CandidTypeText{error_msg}}});
      return;
    }
  }

  // now copy the 'from' file to 'to' file
  std::error_code ec;
  std::filesystem::copy(from_path, to_path, ec);
  if (ec) {
    error_msg = "Error copying file from " + from_path + " to " + to_path;
    ic_api.to_wire(CandidTypeVariant{
        "Err", CandidTypeVariant{"Other", CandidTypeText{error_msg}}});
    return;
  }

  CandidTypeRecord status_code_record;
  status_code_record.append("status_code", CandidTypeNat16{200});
  ic_api.to_wire(CandidTypeVariant{"Ok", status_code_record});
}