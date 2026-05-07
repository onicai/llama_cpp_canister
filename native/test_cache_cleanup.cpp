// Native tests for the recurring prompt-cache cleanup timer.
//
// Strategy:
//   - Test the cleanup body by direct call to run_cache_cleanup_body() so we
//     bypass the candid encode/decode boundary; assertions read the extern
//     counters in cache_cleanup.h directly. `runs` is lifetime-monotonic so
//     we delta-check it; `files_examined`/`files_deleted`/`files_failed` are
//     last-run only so we assert absolute values.
//   - Test the start/stop endpoints via mockIC.run_test, verifying timer
//     registry size via IcTimers::instance().size().
//   - Test access-denied responses via mockIC.run_test with the anonymous
//     principal; output candid matches the project's ACCESS_DENIED_API_ERROR.
//
// Each scenario sets up its own files under
// `.canister_cache/cache_cleanup_test_principal/...` (a fixed unique path so
// other tests do not collide) and tears them down at the end. File mtimes
// are set with std::filesystem::last_write_time using file_clock::now() —
// NOT ic0mock_set_time_override — because cleanup compares ages in the host
// file_clock domain.

#include "test_cache_cleanup.h"

#include "../src/cache_cleanup.h"
#include "../src/upload.h"

#include "ic_timers.h"
#include "mock_ic.h"

// Mock-only helpers for pinning IC_API::time() so the timer-fires test
// is deterministic (the cleanup body itself uses host file_clock for age
// math; ic0mock_set_time_override only affects IC_API::time(), which
// drives IcTimers's deadline scheduling).
#include "ic0.h"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace {

constexpr const char *TEST_PRINCIPAL_DIR = "cache_cleanup_test_principal";

// Path helper: .canister_cache/<test_principal>/sessions/<filename>
std::filesystem::path session_path(const std::string &filename) {
  return std::filesystem::path(".canister_cache") / TEST_PRINCIPAL_DIR /
         "sessions" / filename;
}

// Path helper: .canister_cache/<test_principal>/db_chats/<filename>
std::filesystem::path db_chats_path(const std::string &filename) {
  return std::filesystem::path(".canister_cache") / TEST_PRINCIPAL_DIR /
         "db_chats" / filename;
}

// Create an empty file at `path`, then set its mtime to `mtime`.
bool create_test_file(const std::filesystem::path &path,
                      std::chrono::file_clock::time_point mtime) {
  std::error_code ec;
  std::filesystem::create_directories(path.parent_path(), ec);
  if (ec) {
    std::cerr << "create_directories failed for " << path.parent_path() << ": "
              << ec.message() << '\n';
    return false;
  }
  std::ofstream(path).close();
  std::filesystem::last_write_time(path, mtime, ec);
  if (ec) {
    std::cerr << "last_write_time failed for " << path << ": " << ec.message()
              << '\n';
    return false;
  }
  return true;
}

// Wipe the test_principal dir between scenarios so each starts clean.
void clear_test_dir() {
  std::error_code ec;
  std::filesystem::remove_all(
      std::filesystem::path(".canister_cache") / TEST_PRINCIPAL_DIR, ec);
}

int expect_eq_u64(const char *label, uint64_t actual, uint64_t expected) {
  if (actual != expected) {
    std::cout << "FAIL: " << label << " expected " << expected << ", got "
              << actual << '\n';
    return 1;
  }
  std::cout << "PASS: " << label << " == " << actual << '\n';
  return 0;
}

int expect_file_exists(const char *label, const std::filesystem::path &p,
                       bool expected) {
  std::error_code ec;
  bool actual = std::filesystem::exists(p, ec);
  if (actual != expected) {
    std::cout << "FAIL: " << label << " expected exists=" << expected
              << ", got " << actual << " (path=" << p << ")\n";
    return 1;
  }
  std::cout << "PASS: " << label << " (path=" << p << ", exists=" << actual
            << ")\n";
  return 0;
}

} // namespace

void test_cache_cleanup(MockIC &mockIC) {
  using std::chrono::file_clock;
  using std::chrono::hours;

  std::string controller_principal{MOCKIC_CONTROLLER};
  std::string anonymous_principal{"2vxsx-fae"};
  bool silent_on_trap = true;

  // didc encode '()'
  const std::string EMPTY_INPUT = "4449444c0000";
  // didc encode '(variant { Err = variant { Other = "Access Denied" } })'
  const std::string ACCESS_DENIED_API_ERROR =
      "4449444c026b01b0ad8fcd0c716b01c5fed20100010100000d4163636573732044656e696"
      "564";

  int extra_failures = 0;

  std::cout << "\n========== test_cache_cleanup ==========\n";

  // -----------------------------------------------------------------------
  // Scenario 1: cache_cleanup_now is a no-op when all files are fresh.
  // -----------------------------------------------------------------------
  clear_test_dir();
  {
    auto fresh = file_clock::now() - hours{1}; // 1h old, TTL is 6h
    create_test_file(session_path("fresh_a.cache"), fresh);
    create_test_file(session_path("fresh_b.cache"), fresh);
    create_test_file(session_path("fresh_c.cache"), fresh);

    uint64_t runs_before = g_cleanup_runs;
    run_cache_cleanup_body();

    // runs is a lifetime counter — assert delta. files_* are last-run only —
    // assert absolute (overwritten on every cleanup invocation).
    extra_failures += expect_eq_u64("[no-op fresh] runs delta",
                                    g_cleanup_runs - runs_before, 1);
    extra_failures += expect_eq_u64("[no-op fresh] files_deleted (last run)",
                                    g_cleanup_files_deleted, 0);
    extra_failures += expect_eq_u64("[no-op fresh] files_failed (last run)",
                                    g_cleanup_files_failed, 0);
    extra_failures += expect_eq_u64("[no-op fresh] files_examined (last run)",
                                    g_cleanup_files_examined, 3);
    extra_failures += expect_file_exists("[no-op fresh] fresh_a.cache survives",
                                         session_path("fresh_a.cache"), true);
  }

  // -----------------------------------------------------------------------
  // Scenario 2: cache_cleanup_now deletes stale files, keeps fresh.
  // -----------------------------------------------------------------------
  clear_test_dir();
  {
    auto fresh = file_clock::now() - hours{1}; // 1h
    auto stale = file_clock::now() - hours{7}; // 7h, > 6h TTL
    create_test_file(session_path("fresh_1.cache"), fresh);
    create_test_file(session_path("fresh_2.cache"), fresh);
    create_test_file(session_path("stale_1.cache"), stale);
    create_test_file(session_path("stale_2.cache"), stale);
    create_test_file(session_path("stale_3.cache"), stale);

    run_cache_cleanup_body();

    extra_failures += expect_eq_u64("[stale-mix] files_deleted (last run)",
                                    g_cleanup_files_deleted, 3);
    extra_failures += expect_eq_u64("[stale-mix] files_examined (last run)",
                                    g_cleanup_files_examined, 5);
    extra_failures += expect_file_exists("[stale-mix] fresh_1 survives",
                                         session_path("fresh_1.cache"), true);
    extra_failures += expect_file_exists("[stale-mix] stale_1 deleted",
                                         session_path("stale_1.cache"), false);
    extra_failures += expect_file_exists("[stale-mix] stale_3 deleted",
                                         session_path("stale_3.cache"), false);
  }

  // -----------------------------------------------------------------------
  // Scenario 3: Cleanup honors the "sessions/" parent-dir filter — files
  // under db_chats/ (or any other sibling) are NOT touched even when stale.
  // -----------------------------------------------------------------------
  clear_test_dir();
  {
    auto stale = file_clock::now() - hours{7};
    create_test_file(session_path("stale_in_sessions.cache"), stale);
    create_test_file(db_chats_path("stale_in_db_chats.txt"), stale);

    run_cache_cleanup_body();

    extra_failures += expect_eq_u64(
        "[scope] files_deleted (last run, only sessions/* counted)",
        g_cleanup_files_deleted, 1);
    extra_failures +=
        expect_file_exists("[scope] sessions/stale removed",
                           session_path("stale_in_sessions.cache"), false);
    extra_failures +=
        expect_file_exists("[scope] db_chats/stale UNTOUCHED",
                           db_chats_path("stale_in_db_chats.txt"), true);
  }

  // -----------------------------------------------------------------------
  // Scenario 4: Per-tick cap is honored. With 10 stale files and the cap
  // set to 3, a single body invocation deletes exactly 3.
  // -----------------------------------------------------------------------
  clear_test_dir();
  {
    auto stale = file_clock::now() - hours{7};
    for (int i = 0; i < 10; ++i) {
      create_test_file(session_path("capped_" + std::to_string(i) + ".cache"),
                       stale);
    }

    uint64_t saved_cap = g_cleanup_max_files_per_run;
    g_cleanup_max_files_per_run = 3;

    run_cache_cleanup_body();

    extra_failures +=
        expect_eq_u64("[cap=3] files_deleted (last run) after one tick",
                      g_cleanup_files_deleted, 3);
    extra_failures +=
        expect_eq_u64("[cap=3] files_examined (last run) bounded by cap",
                      g_cleanup_files_examined, 3);

    // Restore the default cap so subsequent tests are not affected.
    g_cleanup_max_files_per_run = saved_cap;
  }

  // -----------------------------------------------------------------------
  // Scenario 4b: TTL config controls deletion. Set TTL=2h, create files
  // straddling the 2h boundary, run the body, and verify exactly the
  // stale-side files are deleted. Proves age-vs-ttl arithmetic is honest
  // and that set_cache_cleanup_config(ttl_seconds=...) genuinely changes
  // behavior — not just stored as a number.
  // -----------------------------------------------------------------------
  clear_test_dir();
  {
    using std::chrono::minutes;
    uint64_t saved_ttl = g_cleanup_ttl_ns;
    g_cleanup_ttl_ns = 2ULL * 3600 * 1'000'000'000ULL; // 2h

    auto t_now = file_clock::now();
    create_test_file(session_path("ttl_age_1h.cache"), t_now - hours{1});
    create_test_file(session_path("ttl_age_119min.cache"),
                     t_now - minutes{119});
    create_test_file(session_path("ttl_age_121min.cache"),
                     t_now - minutes{121});
    create_test_file(session_path("ttl_age_3h.cache"), t_now - hours{3});

    run_cache_cleanup_body();

    extra_failures += expect_eq_u64(
        "[ttl=2h] files_examined (last run) == 4 (all four scanned)",
        g_cleanup_files_examined, 4);
    extra_failures += expect_eq_u64(
        "[ttl=2h] files_deleted (last run) == 2 (only the >2h ones)",
        g_cleanup_files_deleted, 2);
    extra_failures +=
        expect_file_exists("[ttl=2h] 1h file kept (well-fresh)",
                           session_path("ttl_age_1h.cache"), true);
    extra_failures += expect_file_exists(
        "[ttl=2h] 119min file kept (just-fresh, 1 min before TTL)",
        session_path("ttl_age_119min.cache"), true);
    extra_failures += expect_file_exists(
        "[ttl=2h] 121min file deleted (just-stale, 1 min after TTL)",
        session_path("ttl_age_121min.cache"), false);
    extra_failures +=
        expect_file_exists("[ttl=2h] 3h file deleted (well-stale)",
                           session_path("ttl_age_3h.cache"), false);

    g_cleanup_ttl_ns = saved_ttl;
  }

  // -----------------------------------------------------------------------
  // Scenario 4c: Recurring timer actually fires the cleanup body after the
  // configured period elapses, driven through IcTimers::dispatch_due. We
  // pin IC_API::time() (which the timer scheduler uses for deadline math)
  // via ic0mock_set_time_override so the test is fully deterministic — no
  // wall-clock sleeps. The cleanup body's age comparison uses host
  // file_clock and is independent of the IC time override; with no files
  // in the directory, the body just bumps g_cleanup_runs.
  // -----------------------------------------------------------------------
  clear_test_dir();
  IC_API::cancel_all_timers();
  {
    uint64_t saved_period = g_cleanup_period_ns;
    g_cleanup_period_ns = 1; // 1ns period — first deadline = now + 1

    ic0mock_set_time_override(1000);
    uint64_t runs_before = g_cleanup_runs;

    // Arm the recurring timer at IC time 1000.
    mockIC.run_test("cache_cleanup_start_timer (timer-fires scenario)",
                    cache_cleanup_start_timer, EMPTY_INPUT, "", silent_on_trap,
                    controller_principal);
    extra_failures +=
        expect_eq_u64("[timer-fires] timer registered after start_timer",
                      IcTimers::instance().size(), 1);

    // Advance IC time past the deadline (1000 + 1 = 1001) and dispatch.
    ic0mock_set_time_override(2000);
    IcTimers::instance().dispatch_due(IC_API::time());

    extra_failures += expect_eq_u64(
        "[timer-fires] cleanup body ran exactly once via timer dispatch",
        g_cleanup_runs - runs_before, 1);

    // A second dispatch at the same now fires again (recurring with 1ns
    // period leaves the next deadline at 1001 + advance*period; advance
    // is computed in the catch-up math which always lands strictly past
    // now_ns). Drive a third dispatch with a further-advanced clock to
    // confirm the recurring nature: a one-shot would NOT fire again.
    ic0mock_set_time_override(3000);
    IcTimers::instance().dispatch_due(IC_API::time());
    extra_failures += expect_eq_u64(
        "[timer-fires] recurring fires again after another period elapses",
        g_cleanup_runs - runs_before, 2);

    // Cleanup
    IC_API::cancel_all_timers();
    g_cleanup_period_ns = saved_period;
    ic0mock_clear_time_override();
  }

  // -----------------------------------------------------------------------
  // Scenario 5: cache_cleanup_start_timer arms the timer; cache_cleanup_stop_timer
  // cancels it. Verified via IcTimers::instance().size().
  // -----------------------------------------------------------------------
  clear_test_dir();
  IC_API::cancel_all_timers(); // start each scenario from a clean registry
  {
    extra_failures += expect_eq_u64("[start] timer registry empty pre-start",
                                    IcTimers::instance().size(), 0);

    mockIC.run_test("cache_cleanup_start_timer", cache_cleanup_start_timer,
                    EMPTY_INPUT, "", silent_on_trap, controller_principal);

    extra_failures +=
        expect_eq_u64("[start] timer registry size == 1 after start",
                      IcTimers::instance().size(), 1);

    // Idempotent: second start should not double-register.
    mockIC.run_test("cache_cleanup_start_timer (idempotent)",
                    cache_cleanup_start_timer, EMPTY_INPUT, "", silent_on_trap,
                    controller_principal);

    extra_failures += expect_eq_u64(
        "[idempotent] timer registry size still 1 after second start",
        IcTimers::instance().size(), 1);

    mockIC.run_test("cache_cleanup_stop_timer", cache_cleanup_stop_timer,
                    EMPTY_INPUT, "", silent_on_trap, controller_principal);

    extra_failures +=
        expect_eq_u64("[stop] timer registry size == 0 after stop",
                      IcTimers::instance().size(), 0);
  }

  // -----------------------------------------------------------------------
  // Scenario 6: Anonymous principal cannot call any of the admin endpoints;
  // each returns the project's ACCESS_DENIED_API_ERROR variant. The query
  // endpoint (get_cache_cleanup_stats) is also gated.
  // -----------------------------------------------------------------------
  IC_API::cancel_all_timers();
  {
    uint64_t runs_before = g_cleanup_runs;

    mockIC.run_test("cache_cleanup_start_timer (anon denied)",
                    cache_cleanup_start_timer, EMPTY_INPUT,
                    ACCESS_DENIED_API_ERROR, silent_on_trap,
                    anonymous_principal);
    mockIC.run_test("cache_cleanup_stop_timer (anon denied)",
                    cache_cleanup_stop_timer, EMPTY_INPUT,
                    ACCESS_DENIED_API_ERROR, silent_on_trap,
                    anonymous_principal);
    mockIC.run_test("cache_cleanup_now (anon denied)", cache_cleanup_now,
                    EMPTY_INPUT, ACCESS_DENIED_API_ERROR, silent_on_trap,
                    anonymous_principal);
    mockIC.run_test("get_cache_cleanup_stats (anon denied)",
                    get_cache_cleanup_stats, EMPTY_INPUT,
                    ACCESS_DENIED_API_ERROR, silent_on_trap,
                    anonymous_principal);
    // set_cache_cleanup_config takes a non-empty input record. Auth is
    // checked BEFORE from_wire, so an EMPTY_INPUT still triggers the auth
    // path correctly (the function returns before attempting to decode).
    mockIC.run_test("set_cache_cleanup_config (anon denied)",
                    set_cache_cleanup_config, EMPTY_INPUT,
                    ACCESS_DENIED_API_ERROR, silent_on_trap,
                    anonymous_principal);

    extra_failures += expect_eq_u64("[anon-denied] no cleanup runs occurred",
                                    g_cleanup_runs - runs_before, 0);
    extra_failures += expect_eq_u64("[anon-denied] timer registry still empty",
                                    IcTimers::instance().size(), 0);
  }

  // Final cleanup so leftover state from this test does not affect later
  // tests in the same mockic.exe run (test_qwen2 etc may scan filesystem).
  clear_test_dir();
  IC_API::cancel_all_timers();

  std::cout << "test_cache_cleanup extra_failures: " << extra_failures
            << "\n========================================\n\n";
  if (extra_failures > 0) {
    // Surface the failure count via mockIC's pass/fail summary by running a
    // synthetic failing test. (mockIC.test_summary() returns 1 if any
    // run_test failed; the extra_failures don't go through run_test.)
    mockIC.run_test(
        "test_cache_cleanup: extra_failures detected (see PASS/FAIL log above)",
        cache_cleanup_now, EMPTY_INPUT, "DELIBERATE_FAIL_TO_RAISE_ALARM",
        silent_on_trap, controller_principal);
  }
}
