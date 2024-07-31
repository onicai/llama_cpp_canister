#include "ready.h"
#include "http.h"

#include <iostream>
#include <string>

#include "ic_api.h"

bool ready_for_inference{false};

// readiness endpoint (ready for inference)
void ready() {
  IC_API ic_api(CanisterQuery{std::string(__func__)}, false);

  if (!ready_for_inference) {
    std::string error_msg =
        "Model not yet uploaded or initialize endpoint not yet called";
    ic_api.to_wire(CandidTypeVariant{
        "Err", CandidTypeVariant{"Other", CandidTypeText{error_msg}}});
    return;
  }

  CandidTypeRecord status_code_record;
  status_code_record.append("status_code",
                            CandidTypeNat16{Http::StatusCode::OK});
  ic_api.to_wire(CandidTypeVariant{"Ok", status_code_record});
}