#include "health.h"

#include <iostream>
#include <string>

#include "ic_api.h"

// health endpoint (liveness)
void health() {
  IC_API ic_api(CanisterQuery{std::string(__func__)}, false);

  std::cout << "Canister is healthy!" << std::endl;

  CandidTypeRecord status_code_record;
  status_code_record.append("status_code", CandidTypeNat16{200});
  ic_api.to_wire(CandidTypeVariant{"Ok", status_code_record});
}