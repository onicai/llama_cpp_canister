#include "run.h"
#include "main_.h"
#include "utils.h"
#include "common.h"

#include <iostream>
#include <string>
#include <filesystem>

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

std::string canister_path_session(std::string path_session, const std::string &principal_id) {
  // We store the prompt-cache files in a folder named with the principal id of the caller
  //
  // Note: to save multiple conversations per user, the front end can simply assign 
  //       a unique prompt-cache file per conversation, and that will do the job !
  //
  std::string canister_path_session;

  if (!path_session.empty()) {
    // Remove all leading '/'
    size_t pos = path_session.find_first_not_of('/');
    if (pos != std::string::npos) {
        path_session.erase(0, pos);
    } else {
        // If the string only contains slashes, clear it
        path_session.clear();
    }

    // The cache file will be stored in ".cache/<principal_id>/<path_session-with_/replaced-by-_>"
    canister_path_session =  ".canister_cache/" + principal_id + "/" + path_session;  

    // Make sure that the cache directory exists, else llama.cpp cannot create the file
    std::filesystem::path file_path(canister_path_session);
    std::filesystem::path dir_path = file_path.parent_path();    
    if (!dir_path.empty() && !std::filesystem::exists(dir_path)) {
        std::filesystem::create_directories(dir_path);
    }
  }

  return canister_path_session;
}

void new_chat() {
  IC_API ic_api(CanisterUpdate{std::string(__func__)}, false);
  CandidTypePrincipal caller = ic_api.get_caller();
  std::string principal_id = caller.get_text();

  auto [argc, argv, args] = get_args_for_main(ic_api);

  // Create/reset a prompt-cache file to zero length, will reset the LLM state for that conversation
  // Get the cache filename from --prompt-cache in args 
  gpt_params params;
  if (!gpt_params_parse(argc, argv.data(), params)) {
    std::string msg = "Cannot parse args.";
    ic_api.to_wire(CandidTypeVariant{
      "Err", CandidTypeVariant{"Other", CandidTypeText{std::string(__func__) +
                                                        ": " + msg}}});
    return;
  }

  // Each principal has their own cache folder
  std::string path_session = params.path_prompt_cache;
  path_session = canister_path_session(path_session, principal_id);

  std::string msg;
  if (!path_session.empty()) {

    if (std::filesystem::exists(path_session)) {
        // First, remove the file if it exists
        bool success = std::filesystem::remove(path_session);
        if (success) {
            msg = "Cache file " + path_session + " deleted successfully";
        } else {
            msg = "Error deleting cache file " + path_session;
            ic_api.to_wire(CandidTypeVariant{
              "Err", CandidTypeVariant{"Other", CandidTypeText{std::string(__func__) +
                                                                ": " + msg}}});
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
  r_out.append("status", CandidTypeNat16{200});
  r_out.append("output", CandidTypeText{msg});
  ic_api.to_wire(CandidTypeVariant{"Ok", r_out});
}


void run(IC_API &ic_api) {
  CandidTypePrincipal caller = ic_api.get_caller();
  std::string principal_id = caller.get_text();

  // Get the data from the wire and prepare arguments for main_
  auto [argc, argv, args] = get_args_for_main(ic_api);

  // Call main_, just like it is called in the llama-cli app
  main_(argc, argv.data(), principal_id);

  // Return output over the wire
  CandidTypeRecord r_out;
  r_out.append("status", CandidTypeNat16{200}); // TODO: set the status code
  r_out.append("output",
               CandidTypeText{"TODO: we need to add some output here.... "});
  ic_api.to_wire(CandidTypeVariant{"Ok", r_out});
}

void run_query() {
  IC_API ic_api(CanisterQuery{std::string(__func__)}, false);
  run(ic_api);
}
void run_update() {
  IC_API ic_api(CanisterUpdate{std::string(__func__)}, false);
  run(ic_api);
}
