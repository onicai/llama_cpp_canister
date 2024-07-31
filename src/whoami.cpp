#include "whoami.h"

#include <iostream>
#include <string>

#include "ic_api.h"

void whoami() {
  IC_API ic_api(CanisterQuery{std::string(__func__)}, false);
  CandidTypePrincipal caller = ic_api.get_caller();
  ic_api.to_wire(CandidTypeText{caller.get_text()});
}