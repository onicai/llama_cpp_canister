// Native tests for the recurring cycle-balance monitor.
//
// Strategy:
//   - Test the refresh worker by direct call to refresh_cycle_balance_body()
//     so we bypass the candid encode/decode boundary; assertions read the
//     extern state in cycle_balance.h directly. The MockIC seeds the cycle
//     balance with MOCKIC_INITIAL_CYCLES_BALANCE (3'000'000'000'000), and
//     IC_API::time() is pinned via ic0mock_set_time_override so updated_at_ns
//     is deterministic.
//   - Test the start/stop endpoints via mockIC.run_test, verifying timer
//     registry size via IcTimers::instance().size() and the g_*_timer_id.
//   - Test the "tracking is off" and access-denied responses via
//     mockIC.run_test; the expected candid hex is the ACTUAL C++ output of
//     CandidTypeVariant{"Err", CandidTypeVariant{"Other", text}} (the type
//     table ordering differs from didc's encoding — see CLAUDE.md tip).

#include "test_cycle_balance.h"

#include "../src/cycle_balance.h"

#include "ic0.h"
#include "ic_api.h"
#include "ic_timers.h"
#include "mock_ic.h"

#include <cstdint>
#include <iostream>
#include <string>

namespace {

int expect_eq_u64(const char *label, uint64_t actual, uint64_t expected) {
  if (actual != expected) {
    std::cout << "FAIL: " << label << " expected " << expected << ", got "
              << actual << '\n';
    return 1;
  }
  std::cout << "PASS: " << label << " == " << actual << '\n';
  return 0;
}

int expect_true(const char *label, bool cond) {
  if (!cond) {
    std::cout << "FAIL: " << label << '\n';
    return 1;
  }
  std::cout << "PASS: " << label << '\n';
  return 0;
}

} // namespace

void test_cycle_balance(MockIC &mockIC) {
  std::string controller_principal{MOCKIC_CONTROLLER};
  std::string anonymous_principal{"2vxsx-fae"};
  bool silent_on_trap = true;

  // didc encode '()'
  const std::string EMPTY_INPUT = "4449444c0000";
  // ACTUAL C++ output of send_access_denied_api_error (matches
  // test_cache_cleanup's hardcoded value).
  const std::string ACCESS_DENIED_API_ERROR =
      "4449444c026b01b0ad8fcd0c716b01c5fed20100010100000d4163636573732044656e696"
      "564";
  // ACTUAL C++ output for the "tracking is off" error. Same nested
  // Err/Other/text structure as ACCESS_DENIED — only the trailing length byte
  // (0x4e = 78) and the UTF-8 text bytes differ (note the em-dash e2 80 94).
  const std::string TRACKING_OFF_ERROR =
      "4449444c026b01b0ad8fcd0c716b01c5fed20100010100004e6379636c652062616c616e63"
      "6520747261636b696e67206973206f666620e2809420616e2061646d696e206d7573742063"
      "616c6c206379636c655f62616c616e63655f73746172745f74696d6572";

  int extra_failures = 0;

  std::cout << "\n========== test_cycle_balance ==========\n";

  IC_API::cancel_all_timers();
  g_cycle_balance_timer_id = 0;

  // -----------------------------------------------------------------------
  // Scenario 1: the refresh worker caches the (mock) cycle balance and the
  // current IC time.
  // -----------------------------------------------------------------------
  {
    ic0mock_set_time_override(123456789);
    g_cycle_balance = 0;
    g_cycle_balance_updated_at_ns = 0;

    refresh_cycle_balance_body();

    extra_failures += expect_true(
        "[refresh] cycle_balance == MOCKIC initial 3'000'000'000'000",
        g_cycle_balance == static_cast<__uint128_t>(3000000000000ULL));
    extra_failures += expect_eq_u64("[refresh] updated_at_ns == time override",
                                    g_cycle_balance_updated_at_ns, 123456789);

    ic0mock_clear_time_override();
  }

  // -----------------------------------------------------------------------
  // Scenario 2: get_cycle_balance returns the clear "tracking is off" error
  // when the timer is not armed (g_cycle_balance_timer_id == 0).
  // -----------------------------------------------------------------------
  IC_API::cancel_all_timers();
  g_cycle_balance_timer_id = 0;
  mockIC.run_test("get_cycle_balance (tracking off)", get_cycle_balance,
                  EMPTY_INPUT, TRACKING_OFF_ERROR, silent_on_trap,
                  controller_principal);

  // -----------------------------------------------------------------------
  // Scenario 3: the anonymous principal is denied on every endpoint (auth is
  // checked before the tracking-off check, so it short-circuits to denied).
  // -----------------------------------------------------------------------
  mockIC.run_test("get_cycle_balance (anon denied)", get_cycle_balance,
                  EMPTY_INPUT, ACCESS_DENIED_API_ERROR, silent_on_trap,
                  anonymous_principal);
  mockIC.run_test("cycle_balance_start_timer (anon denied)",
                  cycle_balance_start_timer, EMPTY_INPUT,
                  ACCESS_DENIED_API_ERROR, silent_on_trap, anonymous_principal);
  mockIC.run_test("cycle_balance_stop_timer (anon denied)",
                  cycle_balance_stop_timer, EMPTY_INPUT,
                  ACCESS_DENIED_API_ERROR, silent_on_trap, anonymous_principal);

  // -----------------------------------------------------------------------
  // Scenario 4: start arms the timer (and refreshes once); stop cancels it.
  // -----------------------------------------------------------------------
  extra_failures += expect_eq_u64("[start] registry empty pre-start",
                                  IcTimers::instance().size(), 0);

  mockIC.run_test("cycle_balance_start_timer", cycle_balance_start_timer,
                  EMPTY_INPUT, "", silent_on_trap, controller_principal);

  extra_failures += expect_eq_u64("[start] registry size == 1 after start",
                                  IcTimers::instance().size(), 1);
  extra_failures += expect_true("[start] timer id set after start",
                                g_cycle_balance_timer_id != 0);

  // Idempotent: second start must not double-register.
  mockIC.run_test("cycle_balance_start_timer (idempotent)",
                  cycle_balance_start_timer, EMPTY_INPUT, "", silent_on_trap,
                  controller_principal);
  extra_failures += expect_eq_u64("[idempotent] registry still 1",
                                  IcTimers::instance().size(), 1);

  // With tracking on, the query returns Ok (no trap; value carries a
  // timestamp so we don't pin the exact output hex here — the smoke test
  // verifies the Ok content over real Candid).
  mockIC.run_test("get_cycle_balance (ok path, tracking on)", get_cycle_balance,
                  EMPTY_INPUT, "", silent_on_trap, controller_principal);

  mockIC.run_test("cycle_balance_stop_timer", cycle_balance_stop_timer,
                  EMPTY_INPUT, "", silent_on_trap, controller_principal);

  extra_failures += expect_eq_u64("[stop] registry size == 0 after stop",
                                  IcTimers::instance().size(), 0);
  extra_failures += expect_true("[stop] timer id reset after stop",
                                g_cycle_balance_timer_id == 0);

  // Cleanup so leftover timers/state do not affect later tests.
  IC_API::cancel_all_timers();
  g_cycle_balance_timer_id = 0;

  std::cout << "test_cycle_balance extra_failures: " << extra_failures
            << "\n========================================\n\n";
  if (extra_failures > 0) {
    // Surface the failure count via mockIC's pass/fail summary by running a
    // synthetic failing test (extra_failures don't flow through run_test).
    mockIC.run_test(
        "test_cycle_balance: extra_failures detected (see PASS/FAIL log above)",
        get_cycle_balance, EMPTY_INPUT, "DELIBERATE_FAIL_TO_RAISE_ALARM",
        silent_on_trap, controller_principal);
  }
}
