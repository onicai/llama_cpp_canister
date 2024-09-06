// load the model, which will be Orthogonally Persisted

#include "model.h"
#include "auth.h"
#include "http.h"
#include "main_.h"
#include "ready.h"
#include "upload.h"
#include "utils.h"

#include <iostream>
#include <string>

#include "ic_api.h"

void load_model() {
  IC_API ic_api(CanisterUpdate{std::string(__func__)}, false);
  if (!is_caller_a_controller(ic_api)) return;

  CandidTypePrincipal caller = ic_api.get_caller();
  std::string principal_id = caller.get_text();

  // Get the data from the wire and prepare arguments for main_
  auto [argc, argv, args] = get_args_for_main(ic_api);

  // First free the OP memory of a previously loaded model
  free_model();

  // Call main_, just like it is called in the llama-cli app
  main_(argc, argv.data(), principal_id);

  // If we get this far, everything is Ok and ready to be used
  ready_for_inference = true;

  CandidTypeRecord status_code_record;
  status_code_record.append("status_code",
                            CandidTypeNat16{Http::StatusCode::OK});
  ic_api.to_wire(CandidTypeVariant{"Ok", status_code_record});
}