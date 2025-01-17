// canister_init, and health & ready endpoints
#include "canister.h"

#include "auth.h"
#include "http.h"
#include "ic_api.h"
#include <array>
#include <string>
#include <vector>

static uint16_t access_level = 0;
static const std::array<std::string, 2> access_levels = {
    "Only controllers", "All except anonymous"};

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

// Checks if caller is allowed to call canister and if not, optionally write an error to the wire
bool is_caller_whitelisted(IC_API &ic_api, bool err_to_wire) {
  CandidTypePrincipal caller = ic_api.get_caller();

  // Canister controller is always whitelisted
  if (ic_api.is_controller(caller)) return true;

  // Check if all non-anonymous callers are allowed and the caller is not anonymous
  if (access_level == 1 && !caller.is_anonymous()) return true;

  // Caller is not whitelisted
  if (err_to_wire) {
    std::string error_msg = "Access Denied";
    ic_api.to_wire(CandidTypeVariant{
        "Err", CandidTypeVariant{"Other", CandidTypeText{error_msg}}});
  }
  return false;
}

std::string get_explanation_() {
  if (access_level == 1) return access_levels[1];
  return access_levels[0];
}

void set_access() {
  IC_API ic_api(CanisterUpdate{std::string(__func__)}, false);
  if (!is_caller_a_controller(ic_api)) return;

  // Get the level from the wire
  uint16_t level{0};
  CandidTypeRecord r_in;
  r_in.append("level", CandidTypeNat16{&level});
  ic_api.from_wire(r_in);

  if (level > 1) {
    std::string error_msg = "Invalid access level";
    ic_api.to_wire(CandidTypeVariant{
        "Err", CandidTypeVariant{"Other", CandidTypeText{error_msg}}});
    return;
  }

  access_level = level;

  // Return the status over the wire
  CandidTypeRecord access_record;
  access_record.append("level", CandidTypeNat16{access_level});
  access_record.append("explanation", CandidTypeText{get_explanation_()});
  ic_api.to_wire(CandidTypeVariant{"Ok", CandidTypeRecord{access_record}});
}

void get_access() {
  IC_API ic_api(CanisterQuery{std::string(__func__)}, false);
  if (!is_caller_a_controller(ic_api)) return;

  // Return the status over the wire
  CandidTypeRecord access_record;
  access_record.append("level", CandidTypeNat16{access_level});
  access_record.append("explanation", CandidTypeText{get_explanation_()});
  ic_api.to_wire(CandidTypeVariant{"Ok", CandidTypeRecord{access_record}});
}

void check_access() {
  IC_API ic_api(CanisterQuery{std::string(__func__)}, false);
  if (!is_caller_whitelisted(ic_api)) return;

  CandidTypeRecord status_code_record;
  status_code_record.append("status_code",
                            CandidTypeNat16{Http::StatusCode::OK});
  ic_api.to_wire(CandidTypeVariant{"Ok", status_code_record});
}