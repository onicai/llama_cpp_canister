#include "utils.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdio.h>
#include <string>

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
