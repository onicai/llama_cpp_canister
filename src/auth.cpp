// canister_init, and health & ready endpoints
#include "canister.h"

#include "auth.h"
#include "http.h"
#include "ic_api.h"
#include "utils.h"
#include <algorithm>
#include <array>
#include <string>
#include <vector>

static uint16_t access_level = 0;
static const std::array<std::string, 2> access_levels = {
    "Only controllers", "All except anonymous"};

// Admin RBAC Storage
static std::vector<AdminRoleAssignment> admin_role_assignments;

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

// Error senders for different result type families
void send_access_denied_api_error(IC_API &ic_api) {
  ic_api.to_wire(CandidTypeVariant{
      "Err", CandidTypeVariant{"Other", CandidTypeText{"Access Denied"}}});
}

void send_access_denied_output_record(IC_API &ic_api) {
  std::string error_msg = "Access Denied";
  send_output_record_result_error_to_wire(ic_api, Http::StatusCode::Unauthorized,
                                          error_msg);
}

// Admin RBAC helper
std::optional<AdminRoleAssignment>
get_admin_role_for_principal(const std::string &principal) {
  for (const auto &assignment : admin_role_assignments) {
    if (assignment.principal == principal) {
      return assignment;
    }
  }
  return std::nullopt;
}

// Admin RBAC auth check functions
bool has_admin_query_role(IC_API &ic_api) {
  CandidTypePrincipal caller = ic_api.get_caller();
  if (ic_api.is_controller(caller)) return true;

  std::string principal_text = caller.get_text();
  auto role_opt = get_admin_role_for_principal(principal_text);

  if (role_opt.has_value()) {
    // AdminUpdate includes AdminQuery permissions
    if (role_opt->role == AdminRole::AdminQuery ||
        role_opt->role == AdminRole::AdminUpdate) {
      return true;
    }
  }
  return false;
}

bool has_admin_update_role(IC_API &ic_api) {
  CandidTypePrincipal caller = ic_api.get_caller();
  if (ic_api.is_controller(caller)) return true;

  std::string principal_text = caller.get_text();
  auto role_opt = get_admin_role_for_principal(principal_text);

  if (role_opt.has_value() && role_opt->role == AdminRole::AdminUpdate) {
    return true;
  }
  return false;
}

bool has_admin_query_or_whitelisted(IC_API &ic_api) {
  if (has_admin_query_role(ic_api)) return true;
  if (is_caller_whitelisted(ic_api, false)) return true;
  return false;
}

bool has_admin_update_or_whitelisted(IC_API &ic_api) {
  if (has_admin_update_role(ic_api)) return true;
  if (is_caller_whitelisted(ic_api, false)) return true;
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
  if (!has_admin_query_role(ic_api)) {
    send_access_denied_api_error(ic_api);
    return;
  }

  // Return the status over the wire
  CandidTypeRecord access_record;
  access_record.append("level", CandidTypeNat16{access_level});
  access_record.append("explanation", CandidTypeText{get_explanation_()});
  ic_api.to_wire(CandidTypeVariant{"Ok", CandidTypeRecord{access_record}});
}

void check_access() {
  IC_API ic_api(CanisterQuery{std::string(__func__)}, false);
  if (!has_admin_query_or_whitelisted(ic_api)) {
    send_access_denied_api_error(ic_api);
    return;
  }

  CandidTypeRecord status_code_record;
  status_code_record.append("status_code",
                            CandidTypeNat16{Http::StatusCode::OK});
  ic_api.to_wire(CandidTypeVariant{"Ok", status_code_record});
}

// Admin RBAC Management Endpoints
void assignAdminRole() {
  IC_API ic_api(CanisterUpdate{std::string(__func__)}, false);

  CandidTypePrincipal caller = ic_api.get_caller();
  if (!ic_api.is_controller(caller)) {
    send_access_denied_api_error(ic_api);
    return;
  }

  // Parse input
  std::string principal_to_assign;
  std::string note;
  std::string role_label;

  CandidTypeRecord r_in;
  r_in.append("principal", CandidTypeText{&principal_to_assign});
  r_in.append("note", CandidTypeText{&note});

  CandidTypeVariant role_variant{&role_label};
  r_in.append("role", role_variant);
  ic_api.from_wire(r_in);

  // Determine role from variant
  AdminRole role;
  if (role_label == "AdminQuery") {
    role = AdminRole::AdminQuery;
  } else if (role_label == "AdminUpdate") {
    role = AdminRole::AdminUpdate;
  } else {
    ic_api.to_wire(CandidTypeVariant{
        "Err", CandidTypeVariant{"Other", CandidTypeText{"Invalid role"}}});
    return;
  }

  // Upsert: remove existing assignment if any
  admin_role_assignments.erase(
      std::remove_if(
          admin_role_assignments.begin(), admin_role_assignments.end(),
          [&](const AdminRoleAssignment &a) {
            return a.principal == principal_to_assign;
          }),
      admin_role_assignments.end());

  // Create new assignment
  AdminRoleAssignment assignment;
  assignment.principal = principal_to_assign;
  assignment.role = role;
  assignment.assignedBy = caller.get_text();
  assignment.assignedAt = ic_api.time();
  assignment.note = note;

  admin_role_assignments.push_back(assignment);

  // Return result
  CandidTypeRecord r_out;
  r_out.append("principal", CandidTypeText{assignment.principal});
  r_out.append("role", CandidTypeVariant{role_label});
  r_out.append("assignedBy", CandidTypeText{assignment.assignedBy});
  r_out.append("assignedAt", CandidTypeNat64{assignment.assignedAt});
  r_out.append("note", CandidTypeText{assignment.note});

  ic_api.to_wire(CandidTypeVariant{"Ok", r_out});
}

void revokeAdminRole() {
  IC_API ic_api(CanisterUpdate{std::string(__func__)}, false);

  CandidTypePrincipal caller = ic_api.get_caller();
  if (!ic_api.is_controller(caller)) {
    send_access_denied_api_error(ic_api);
    return;
  }

  std::string principal_to_revoke;
  ic_api.from_wire(CandidTypeText{&principal_to_revoke});

  auto it = std::find_if(
      admin_role_assignments.begin(), admin_role_assignments.end(),
      [&](const AdminRoleAssignment &a) {
        return a.principal == principal_to_revoke;
      });

  if (it == admin_role_assignments.end()) {
    ic_api.to_wire(CandidTypeVariant{
        "Err",
        CandidTypeVariant{"Other", CandidTypeText{"Principal not found"}}});
    return;
  }

  admin_role_assignments.erase(it);

  ic_api.to_wire(CandidTypeVariant{
      "Ok",
      CandidTypeText{"Admin role revoked for " + principal_to_revoke}});
}

void getAdminRoles() {
  IC_API ic_api(CanisterQuery{std::string(__func__)}, false);

  CandidTypePrincipal caller = ic_api.get_caller();
  if (!ic_api.is_controller(caller)) {
    send_access_denied_api_error(ic_api);
    return;
  }

  // Build vectors for each field
  std::vector<std::string> principals;
  std::vector<std::string> roles;
  std::vector<std::string> assignedBys;
  std::vector<uint64_t> assignedAts;
  std::vector<std::string> notes;

  for (const auto &assignment : admin_role_assignments) {
    principals.push_back(assignment.principal);
    std::string role_label =
        (assignment.role == AdminRole::AdminQuery) ? "AdminQuery" : "AdminUpdate";
    roles.push_back(role_label);
    assignedBys.push_back(assignment.assignedBy);
    assignedAts.push_back(assignment.assignedAt);
    notes.push_back(assignment.note);
  }

  // Create a record template with vector fields
  CandidTypeRecord r_out;
  r_out.append("principal", CandidTypeVecText{principals});
  r_out.append("role", CandidTypeVecText{roles});
  r_out.append("assignedBy", CandidTypeVecText{assignedBys});
  r_out.append("assignedAt", CandidTypeVecNat64{assignedAts});
  r_out.append("note", CandidTypeVecText{notes});

  ic_api.to_wire(CandidTypeVariant{"Ok", CandidTypeVecRecord{r_out}});
}