// Native tests for the settings that survive an upgrade (src/settings.h):
// they are saved on every change, the heap globals are then reset as an
// upgrade would, and canister_post_upgrade must restore them and re-arm a
// running cleanup timer. Runs canister_post_upgrade under icpp-pro's Mock IC,
// which refuses the ic0 calls the replica refuses in that entry point.

#include "test_settings.h"

#include "../src/auth.h"
#include "../src/cache_cleanup.h"
#include "../src/settings.h"

#include "ic_api.h"
#include "ic_timers.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace {

int g_failures = 0;

void check(bool ok, const std::string &what) {
  std::cout << (ok ? "PASS: " : "FAIL: ") << "test_settings: " << what
            << std::endl;
  if (!ok) ++g_failures;
}

} // namespace

void test_settings(MockIC &mockIC) {
  std::string controller{MOCKIC_CONTROLLER};
  std::cout << "\n========== test_settings ==========\n";
  std::error_code ec;
  std::filesystem::remove(SETTINGS_PATH, ec);

  // --- without a settings file the defaults stay
  check(load_settings().find("the defaults apply") != std::string::npos,
        "no file: the defaults apply");

  // --- what an operator sets ...
  restore_access_level(1);
  restore_admin_role_assignments({{"gateway-principal", AdminRole::AdminUpdate,
                                   controller, 42, "the gateway"}});
  const uint64_t ttl_ns = g_cleanup_ttl_ns;
  const uint64_t max_states = g_max_decision_states;
  g_cleanup_ttl_ns = 48ULL * 3600 * 1000000000ULL;
  g_max_decision_states = 99;
  cache_cleanup_rearm();
  save_settings();

  // --- ... is lost from the heap by an upgrade ...
  IC_API::cancel_all_timers();
  g_cleanup_timer_id = 0;
  restore_access_level(0);
  restore_admin_role_assignments({});
  g_cleanup_ttl_ns = ttl_ns;
  g_max_decision_states = max_states;

  // --- ... and restored by canister_post_upgrade
  canister_post_upgrade();
  check(get_access_level() == 1, "access level restored");
  const auto &roles = get_admin_role_assignments();
  check(roles.size() == 1 && roles[0].principal == "gateway-principal" &&
            roles[0].role == AdminRole::AdminUpdate &&
            roles[0].assignedBy == controller && roles[0].assignedAt == 42 &&
            roles[0].note == "the gateway",
        "admin role restored");
  check(g_cleanup_ttl_ns == 48ULL * 3600 * 1000000000ULL &&
            g_max_decision_states == 99,
        "cleanup config restored");
  check(g_cleanup_timer_id != 0 && IcTimers::instance().size() == 1,
        "running cleanup timer re-armed");

  // --- a negative number is not restored as a huge unsigned one
  std::ofstream(SETTINGS_PATH, std::ios::trunc)
      << "{\"cache_cleanup\": {\"ttl_ns\": -1}}";
  load_settings();
  check(g_cleanup_ttl_ns == 48ULL * 3600 * 1000000000ULL,
        "negative value not restored");

  // --- an unreadable file keeps the defaults (and does not trap)
  std::ofstream(SETTINGS_PATH, std::ios::trunc) << "{not json";
  check(load_settings().find("unreadable") != std::string::npos,
        "unreadable file: the defaults apply");

  // --- leave the defaults behind for the other tests
  IC_API::cancel_all_timers();
  g_cleanup_timer_id = 0;
  restore_access_level(0);
  restore_admin_role_assignments({});
  g_cleanup_ttl_ns = ttl_ns;
  g_max_decision_states = max_states;
  std::filesystem::remove(SETTINGS_PATH, ec);

  std::cout << "test_settings failures: " << g_failures
            << "\n===================================\n\n";
  if (g_failures > 0) {
    std::exit(1);
  }
}
