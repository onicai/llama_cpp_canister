#include "run.h"
#include "main_.h"

#include <string>
#include <iostream>

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

void run(IC_API &ic_api) {
  std::cout << "debug: run.cpp - run -01 " << std::endl;
  // Get the data from the wire
  std::vector<std::string> args;

  CandidTypeRecord r_in;
  r_in.append("args", CandidTypeVecText{&args});
  ic_api.from_wire(r_in);

  // The first argv is always the program name
  args.insert(args.begin(), "llama_cpp_canister");

  // Construct argc
  int argc = args.size();

  // Construct argv
  std::vector<char *> argv(argc);
  for (int i = 0; i < argc; ++i) {
    argv[i] = &args[i][0]; // Convert std::string to char*
  }

  std::cout << "debug: run.cpp - run -02 " << std::endl;
  // Print argc and argv
  std::cout << "argc: " << argc << std::endl;
  for (int i = 0; i < argc; ++i) {
      std::cout << "argv[" << i << "] = " << argv[i] << std::endl;
  }

  // Call main_, just like it is called in the console app
  main_(argc, argv.data());

  // Return output over the wire
  CandidTypeRecord r_out;
  r_out.append("status", CandidTypeNat16{200}); // TODO: set the status code
  r_out.append("output",
               CandidTypeText{"TODO: we need to add some output here.... "});
  ic_api.to_wire(CandidTypeVariant{"Ok", r_out});
}

void run_query() {
  std::cout << "debug: run.cpp - run_query - 01 " << std::endl;
  IC_API ic_api(CanisterQuery{std::string(__func__)}, false);
  std::cout << "debug: run.cpp - run_query - 02 " << std::endl;
  run(ic_api);
  std::cout << "debug: run.cpp - run_query - 03 " << std::endl;
}
void run_update() {
  IC_API ic_api(CanisterUpdate{std::string(__func__)}, false);
  run(ic_api);
}
