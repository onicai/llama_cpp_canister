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

  // Lets go.
  ready_for_inference = true;

  // First free the OP memory of a previously loaded model
  free_model();

  // Call main_, just like it is called in the llama-cli app
  std::string icpp_error_msg;
  std::ostringstream input_ss;
  std::ostringstream output_ss;
  bool load_model_only = true;
  int result = main_(argc, argv.data(), principal_id, load_model_only,
                     icpp_error_msg, input_ss, output_ss);

  // Exit if there was an error
  if (result != 0) {
    CandidTypeRecord r_out;
    r_out.append("status_code",
                 CandidTypeNat16{Http::StatusCode::InternalServerError}); // 500
    r_out.append("input", CandidTypeText{""});
    r_out.append("prompt_remaining", CandidTypeText{""});
    r_out.append("output", CandidTypeText{""});
    r_out.append("error", CandidTypeText{icpp_error_msg});
    ic_api.to_wire(CandidTypeVariant{"Err", r_out});
    return;
  }

  // If we get this far, everything is Ok and ready to be used
  ready_for_inference = true;

  CandidTypeRecord r_out;
  r_out.append("status_code", CandidTypeNat16{Http::StatusCode::OK}); // 200
  r_out.append("input", CandidTypeText{""});
  r_out.append("prompt_remaining", CandidTypeText{""});
  r_out.append("output",
               CandidTypeText{"Model succesfully loaded into memory."});
  r_out.append("error", CandidTypeText{""});
  ic_api.to_wire(CandidTypeVariant{"Ok", r_out});
}