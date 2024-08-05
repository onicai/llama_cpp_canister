// canister_init, and health & ready endpoints
#include "canister.h"

#include "http.h"
#include "ic_api.h"
#include <string>

// Checks if caller is controller of the canister and if not, optionally write an error to the wire
bool is_caller_a_controller(IC_API &ic_api, bool err_to_wire) {
  CandidTypePrincipal caller = ic_api.get_caller();
  if (ic_api.is_controller(caller)) return true;
  else {
    if (err_to_wire) {
      std::string error_msg = "Access Denied";
      ic_api.to_wire(CandidTypeVariant{
          "Err", CandidTypeVariant{"Other", CandidTypeText{error_msg}}});
    }
    return false;
  }
}