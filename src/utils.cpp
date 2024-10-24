#include "utils.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdio.h>
#include <string>

#include "http.h"
#include "ic_api.h"

bool open_ifstream(const std::string &filename,
                   const std::ios_base::openmode &mode,
                   std::ifstream &if_stream, std::string &msg) {
  if_stream.open(filename.c_str(), mode);
  if (!if_stream.is_open()) {
    msg = "ERROR: failed to open ifstream for file " + filename;
    return false;
  }
  return true;
}

bool open_ofstream(const std::string &filename,
                   const std::ios_base::openmode &mode,
                   std::ofstream &of_stream, std::string &msg) {
  of_stream.open(filename.c_str(), mode);
  if (!of_stream.is_open()) {
    msg = "ERROR: failed to open ofstream for file " + filename;
    return false;
  }
  return true;
}

std::tuple<int, std::vector<char *>, std::unique_ptr<std::vector<std::string>>>
get_args_for_main(IC_API &ic_api) {
  // Use unique_ptr to ensure the lifetime of args
  auto args = std::make_unique<std::vector<std::string>>();
  CandidTypeRecord r_in;
  r_in.append("args", CandidTypeVecText{args.get()});
  ic_api.from_wire(r_in);

  // The first argv is always the program name
  args->insert(args->begin(), "llama_cpp_canister");

  // Construct argc
  int argc = args->size();

  // Construct argv
  std::vector<char *> argv(argc);
  for (int i = 0; i < argc; ++i) {
    argv[i] = &(*args)[i][0]; // Convert std::string to char*
  }

  // Print argc and argv
  std::cout << "argc: " << argc << std::endl;
  for (int i = 0; i < argc; ++i) {
    std::cout << "argv[" << i << "] = " << argv[i] << std::endl;
  }

  return std::make_tuple(argc, std::move(argv), std::move(args));
}

bool my_create_directory(const std::filesystem::path &dir_path,
                         std::string &error_msg) {
  if (!dir_path.empty() && !std::filesystem::exists(dir_path)) {
    std::error_code ec;
    std::filesystem::create_directories(
        dir_path, ec); // Use the non-exception-throwing version
    if (ec) {
      error_msg = ec.message();
      return false;
    }
  }
  return true;
}

void send_output_record_result_error_to_wire(IC_API &ic_api,
                                             const std::string &error_msg) {
  CandidTypeRecord r_out;
  r_out.append("status_code",
               CandidTypeNat16{Http::StatusCode::InternalServerError}); // 500
  r_out.append("conversation", CandidTypeText{""});
  r_out.append("output", CandidTypeText{""});
  r_out.append("error", CandidTypeText{error_msg});
  r_out.append("prompt_remaining", CandidTypeText{""});
  r_out.append("generated_eog", CandidTypeBool{false});
  ic_api.to_wire(CandidTypeVariant{"Err", r_out});
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

// Helper function to retrieve the last write time of a file
std::filesystem::file_time_type
get_last_write_time(const std::filesystem::path &file, std::error_code &ec) {
  return std::filesystem::last_write_time(file, ec);
}