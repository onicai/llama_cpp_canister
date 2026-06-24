// Recurring canister cycle-balance monitor — implementation.
// See cycle_balance.h for the high-level contract.

#include "cycle_balance.h"

#include "auth.h"
#include "ic0.h"
#include "ic_api.h"

#include <cstdint>
#include <iostream>
#include <string>

// --- Config --------------------------------------------------------------
namespace {
constexpr uint64_t NS_PER_SEC = 1'000'000'000ULL;
constexpr uint64_t CYCLE_BALANCE_PERIOD_NS = 3600ULL * NS_PER_SEC; // 1 hour
} // namespace

// --- File-scope state (extern in cycle_balance.h for native-test access) -
//
// In-memory only; wiped on every canister upgrade. `g_cycle_balance` holds
// the most recent snapshot; `g_cycle_balance_updated_at_ns` is the IC_API
// time at which it was taken; `g_cycle_balance_timer_id` is the active timer
// id (0 = not armed, i.e. tracking is off).
__uint128_t g_cycle_balance = 0;
uint64_t g_cycle_balance_updated_at_ns = 0;
uint64_t g_cycle_balance_timer_id = 0;

namespace {

// Re-arm the recurring timer with the hourly period. Caller must have first
// cancelled any existing timer (id stored in g_cycle_balance_timer_id).
void arm_cycle_balance_timer_() {
  g_cycle_balance_timer_id = IC_API::set_timer_recurring(
      CYCLE_BALANCE_PERIOD_NS, []() { refresh_cycle_balance_body(); });
}

} // namespace

// --- Worker ---------------------------------------------------------------
//
// Reads the live cycle balance via the raw ic0 import — mirrors
// IC_API::get_canister_self_cycle_balance() — so it can run inside the timer
// callback without constructing its own IC_API.
void refresh_cycle_balance_body() {
  __uint128_t bal;
  ic0_canister_cycle_balance128(reinterpret_cast<uintptr_t>(&bal));
  g_cycle_balance = bal;
  g_cycle_balance_updated_at_ns = IC_API::time();
}

// --- Endpoints ------------------------------------------------------------

void cycle_balance_start_timer() {
  IC_API ic_api(CanisterUpdate{std::string(__func__)}, false);
  if (!has_admin_update_role(ic_api)) {
    send_access_denied_api_error(ic_api);
    return;
  }
  ic_api.from_wire();

  // Idempotent: cancel any existing armed timer before re-arming.
  if (g_cycle_balance_timer_id != 0) {
    IC_API::cancel_timer(g_cycle_balance_timer_id);
  }
  // Refresh once immediately so the cached value is meaningful before the
  // first hourly tick (set_timer_recurring first fires one period later).
  refresh_cycle_balance_body();
  arm_cycle_balance_timer_();

  std::string msg = "cycle balance tracking armed; period_seconds=" +
                    std::to_string(CYCLE_BALANCE_PERIOD_NS / NS_PER_SEC);
  std::cout << "llama_cpp: " << std::string(__func__) << " - " << msg
            << std::endl;

  CandidTypeRecord status_code_record;
  status_code_record.append("status_code", CandidTypeNat16{200});
  ic_api.to_wire(CandidTypeVariant{"Ok", status_code_record});
}

void cycle_balance_stop_timer() {
  IC_API ic_api(CanisterUpdate{std::string(__func__)}, false);
  if (!has_admin_update_role(ic_api)) {
    send_access_denied_api_error(ic_api);
    return;
  }
  ic_api.from_wire();

  if (g_cycle_balance_timer_id != 0) {
    IC_API::cancel_timer(g_cycle_balance_timer_id);
    g_cycle_balance_timer_id = 0;
    std::cout << "llama_cpp: " << std::string(__func__)
              << " - cycle balance tracking stopped" << std::endl;
  }

  CandidTypeRecord status_code_record;
  status_code_record.append("status_code", CandidTypeNat16{200});
  ic_api.to_wire(CandidTypeVariant{"Ok", status_code_record});
}

void get_cycle_balance() {
  IC_API ic_api(CanisterQuery{std::string(__func__)}, false);
  if (!has_admin_query_role(ic_api)) {
    send_access_denied_api_error(ic_api);
    return;
  }
  ic_api.from_wire();

  // If the timer is not armed, the cached value is stale/zero — return a
  // clear error instead of a misleading number.
  if (g_cycle_balance_timer_id == 0) {
    ic_api.to_wire(CandidTypeVariant{
        "Err",
        CandidTypeVariant{
            "Other", CandidTypeText{"cycle balance tracking is off — an admin "
                                    "must call cycle_balance_start_timer"}}});
    return;
  }

  CandidTypeRecord r;
  r.append("cycle_balance", CandidTypeNat{g_cycle_balance});
  r.append("updated_at_ns", CandidTypeNat64{g_cycle_balance_updated_at_ns});
  ic_api.to_wire(CandidTypeVariant{"Ok", CandidTypeRecord{r}});
}
