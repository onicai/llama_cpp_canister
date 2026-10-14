// Recurring canister cycle-balance monitor.
//
// The `get_cycle_balance` query returns the live balance (the IC's
// `canister_cycle_balance128` system call), with updated_at_ns = now, whether
// or not the timer runs.
//
// The timer endpoints are kept for compatibility: the timer refreshes a
// cached snapshot hourly, which nothing requires any more. It is not
// auto-armed and its state does not survive an upgrade.
#pragma once

#include "wasm_symbol.h"

#include <cstdint>

// --- Endpoints ------------------------------------------------------------
// Update endpoints — RBAC: has_admin_update_role required.
void cycle_balance_start_timer()
    WASM_SYMBOL_EXPORTED("canister_update cycle_balance_start_timer");
void cycle_balance_stop_timer()
    WASM_SYMBOL_EXPORTED("canister_update cycle_balance_stop_timer");

// Query endpoint — RBAC: has_admin_query_role required.
void get_cycle_balance()
    WASM_SYMBOL_EXPORTED("canister_query get_cycle_balance");

// --- Internal helper ------------------------------------------------------
// Reads the live cycle balance via the raw ic0 import and caches it. Safe to
// call from any context: an entry-point handler that already has its own
// IC_API on the stack, OR the timer callback (which runs inside
// canister_global_timer's CanisterGlobalTimer IC_API frame). This function
// does NOT construct an IC_API instance.
void refresh_cycle_balance_body();

// --- Test introspection (extern for native tests) ------------------------
// These are file-scope globals in cycle_balance.cpp; they are exposed via
// extern declarations here exclusively so native tests in
// native/test_cycle_balance.cpp can read them directly without going through
// Candid. Production code MUST NOT mutate them.
extern __uint128_t g_cycle_balance;
extern uint64_t g_cycle_balance_updated_at_ns;
extern uint64_t g_cycle_balance_timer_id;
