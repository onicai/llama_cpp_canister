// Recurring prompt-cache cleanup timer.
//
// Walks `.canister_cache/<principal>/sessions/` on a configurable schedule
// (default: every 10 minutes) and deletes files whose mtime is older than a
// configurable TTL (default: 6 hours). Each tick is bounded by
// `g_max_files_per_run` to stay under the IC's per-message instruction
// budget; the next tick continues the cleanup.
//
// Lifecycle is operator-driven: the timer is NOT auto-armed in
// canister_init / canister_post_upgrade. The deploy/upgrade workflow must
// explicitly call `cache_cleanup_start_timer` after the canister is
// reachable. Timer state is in-memory only and does not survive an upgrade.
#pragma once

#include "wasm_symbol.h"

#include <cstdint>

// --- Endpoints ------------------------------------------------------------
// Update endpoints — RBAC: has_admin_update_role required.
void cache_cleanup_start_timer()
    WASM_SYMBOL_EXPORTED("canister_update cache_cleanup_start_timer");
void cache_cleanup_stop_timer()
    WASM_SYMBOL_EXPORTED("canister_update cache_cleanup_stop_timer");
void cache_cleanup_now()
    WASM_SYMBOL_EXPORTED("canister_update cache_cleanup_now");
void set_cache_cleanup_config()
    WASM_SYMBOL_EXPORTED("canister_update set_cache_cleanup_config");

// Query endpoint — RBAC: has_admin_query_role required.
void get_cache_cleanup_stats()
    WASM_SYMBOL_EXPORTED("canister_query get_cache_cleanup_stats");

// --- Internal helper ------------------------------------------------------
// Pure data + filesystem work. Safe to call from any context: an entry-point
// handler that already has its own IC_API on the stack, OR the timer
// callback (which runs inside canister_global_timer's CanisterGlobalTimer
// IC_API frame). This function does NOT construct an IC_API instance.
void run_cache_cleanup_body();

// --- Test introspection (extern for native tests) ------------------------
// These are file-scope statics in cache_cleanup.cpp; they are exposed via
// extern declarations here exclusively so native tests in
// native/test_cache_cleanup.cpp can read them directly without going
// through Candid. Production code MUST NOT mutate them.
//
// Semantics — see cache_cleanup.cpp for the full comment:
//   - LIFETIME (accumulate over the canister's life):
//       g_cleanup_runs, g_cleanup_last_run_ns
//   - LAST-RUN (overwritten on every cleanup invocation):
//       g_cleanup_files_examined, g_cleanup_files_deleted,
//       g_cleanup_files_failed
extern uint64_t g_cleanup_runs;
extern uint64_t g_cleanup_files_examined;
extern uint64_t g_cleanup_files_deleted;
extern uint64_t g_cleanup_files_failed;
extern uint64_t g_cleanup_last_run_ns;
extern uint64_t g_cleanup_period_ns;
extern uint64_t g_cleanup_ttl_ns;
extern uint64_t g_cleanup_max_files_per_run;
extern uint64_t g_cleanup_timer_id;
