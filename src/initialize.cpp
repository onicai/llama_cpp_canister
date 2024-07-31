// Initializes llama2's config, weights & tokenizer with the uploaded file-bytes

#include "initialize.h"
#include "http.h"
#include "upload.h"
#include "ready.h"

#include <string>
#include <iostream>

#include "ic_api.h"


void initialize() {
  IC_API ic_api(CanisterUpdate{std::string(__func__)}, false);
  // TODO: if (!is_caller_a_controller(ic_api)) return;
  std::cout << std::string(__func__) << "TODO: allowed by controller only " << std::endl;

  // Load the model
  std::cout << std::string(__func__) << "TODO: load the model... " << std::endl;

  ready_for_inference = true;

  CandidTypeRecord status_code_record;
  status_code_record.append("status_code",
                            CandidTypeNat16{Http::StatusCode::OK});
  ic_api.to_wire(CandidTypeVariant{"Ok", status_code_record});
}