#pragma once

#include "wasm_symbol.h"

#include "ic_api.h"
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

void set_access() WASM_SYMBOL_EXPORTED("canister_update set_access");
void get_access() WASM_SYMBOL_EXPORTED("canister_query get_access");
void check_access() WASM_SYMBOL_EXPORTED("canister_query check_access");

// Admin RBAC Management - camelCase to match funnAI's PoAIW implementation
void assignAdminRole() WASM_SYMBOL_EXPORTED("canister_update assignAdminRole");
void revokeAdminRole() WASM_SYMBOL_EXPORTED("canister_update revokeAdminRole");
void getAdminRoles() WASM_SYMBOL_EXPORTED("canister_query getAdminRoles");

bool is_caller_a_controller(IC_API &ic_api, bool err_to_wire = true);
bool is_caller_whitelisted(IC_API &ic_api, bool err_to_wire = true);

// Admin RBAC Types
enum class AdminRole { AdminQuery, AdminUpdate };

struct AdminRoleAssignment {
  std::string principal;
  AdminRole role;
  std::string assignedBy;
  uint64_t assignedAt;
  std::string note;
};

// Admin RBAC auth check functions
// Return bool only; do NOT send errors to wire.
// Caller must use appropriate error sender for their result type.
bool has_admin_query_role(IC_API &ic_api);
bool has_admin_update_role(IC_API &ic_api);
bool has_admin_query_or_whitelisted(IC_API &ic_api);
bool has_admin_update_or_whitelisted(IC_API &ic_api);

// Error senders for access denied responses
void send_access_denied_api_error(IC_API &ic_api);
void send_access_denied_output_record(IC_API &ic_api);

std::optional<AdminRoleAssignment>
get_admin_role_for_principal(const std::string &principal);