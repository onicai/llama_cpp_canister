// Recurring prompt-cache cleanup timer — implementation.
// See cache_cleanup.h for the high-level contract.

#include "cache_cleanup.h"

#include "auth.h"
#include "decision.h"
#include "ic_api.h"
#include "settings.h"
#include "upload.h"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <limits>
#include <optional>
#include <system_error>

// --- Defaults & bounds ---------------------------------------------------
namespace {
constexpr uint64_t NS_PER_SEC = 1'000'000'000ULL;
constexpr uint64_t MAX_SECONDS =
    std::numeric_limits<uint64_t>::max() / NS_PER_SEC;

constexpr uint64_t DEFAULT_PERIOD_NS = 600ULL * NS_PER_SEC;   // 10 min
constexpr uint64_t DEFAULT_TTL_NS = 6ULL * 3600 * NS_PER_SEC; // 6 h
constexpr uint64_t DEFAULT_MAX_FILES_PER_RUN = 256;
constexpr uint64_t DEFAULT_MAX_DECISION_STATES = 256; // ~20 MB each

constexpr uint64_t MAX_FILES_PER_RUN_FLOOR = 1;
constexpr uint64_t MAX_FILES_PER_RUN_CEILING = 10'000;
} // namespace

// --- File-scope state (extern in cache_cleanup.h for native-test access) -
//
// Two flavors of stats:
//   - LIFETIME counters (monotonic, accumulate over the canister's life):
//       g_cleanup_runs        — total number of cleanup invocations
//       g_cleanup_last_run_ns — timestamp of the most recent invocation
//   - LAST-RUN stats (overwritten on every cleanup invocation):
//       g_cleanup_files_examined — files inspected in the most recent run
//       g_cleanup_files_deleted  — files removed in the most recent run
//       g_cleanup_files_failed   — failures in the most recent run
// The last-run flavor matches the natural reading of those field names in
// the candid stats record: callers want "what did the last cleanup do?",
// not "what is the lifetime total?". If a lifetime total is needed later,
// add separate `g_cleanup_total_files_*` counters alongside.
uint64_t g_cleanup_runs = 0;
uint64_t g_cleanup_files_examined = 0;
uint64_t g_cleanup_files_deleted = 0;
uint64_t g_cleanup_files_failed = 0;
uint64_t g_cleanup_last_run_ns = 0;
uint64_t g_cleanup_period_ns = DEFAULT_PERIOD_NS;
uint64_t g_cleanup_ttl_ns = DEFAULT_TTL_NS;
uint64_t g_cleanup_max_files_per_run = DEFAULT_MAX_FILES_PER_RUN;
uint64_t g_cleanup_timer_id = 0;
uint64_t g_max_decision_states = DEFAULT_MAX_DECISION_STATES;

namespace {

// Re-arm the recurring timer with the current g_cleanup_period_ns.
// Caller must have first cancelled any existing timer (id stored in
// g_cleanup_timer_id).
void arm_timer_() {
  g_cleanup_timer_id = IC_API::set_timer_recurring(
      g_cleanup_period_ns, []() { run_cache_cleanup_body(); });
}

std::optional<uint64_t> opt_max_decision_states_() {
  return g_max_decision_states;
}

// Helper: append the current stats to a CandidTypeRecord. Used by the
// cache_cleanup_now and get_cache_cleanup_stats endpoints to construct
// their return record consistently.
CandidTypeRecord build_stats_record_() {
  CandidTypeRecord r;
  r.append("runs", CandidTypeNat64{g_cleanup_runs});
  r.append("files_examined", CandidTypeNat64{g_cleanup_files_examined});
  r.append("files_deleted", CandidTypeNat64{g_cleanup_files_deleted});
  r.append("files_failed", CandidTypeNat64{g_cleanup_files_failed});
  r.append("last_run_ns", CandidTypeNat64{g_cleanup_last_run_ns});
  r.append("period_seconds", CandidTypeNat64{g_cleanup_period_ns / NS_PER_SEC});
  r.append("ttl_seconds", CandidTypeNat64{g_cleanup_ttl_ns / NS_PER_SEC});
  r.append("max_files_per_run", CandidTypeNat64{g_cleanup_max_files_per_run});
  r.append("is_running", CandidTypeBool{g_cleanup_timer_id != 0});
  r.append("max_decision_states",
           CandidTypeOptNat64{opt_max_decision_states_()});
  return r;
}

CandidTypeRecord build_config_record_() {
  CandidTypeRecord r;
  r.append("period_seconds", CandidTypeNat64{g_cleanup_period_ns / NS_PER_SEC});
  r.append("ttl_seconds", CandidTypeNat64{g_cleanup_ttl_ns / NS_PER_SEC});
  r.append("max_files_per_run", CandidTypeNat64{g_cleanup_max_files_per_run});
  r.append("is_running", CandidTypeBool{g_cleanup_timer_id != 0});
  r.append("max_decision_states",
           CandidTypeOptNat64{opt_max_decision_states_()});
  return r;
}

CandidTypeRecord build_action_record_() {
  CandidTypeRecord r;
  r.append("ok", CandidTypeBool{true});
  r.append("is_running", CandidTypeBool{g_cleanup_timer_id != 0});
  return r;
}

} // namespace

// --- Cleanup body ---------------------------------------------------------
//
// Walks `.canister_cache/<principal>/sessions/` and the scope directories
// below it (`sessions/<scope>/`, written by run_decision for a caller that
// passes a scope) with non-recursive directory_iterators, which map 1:1 to
// the on-disk layout. Pinned kev states (and their stamp & pin files) are
// skipped while their pin lasts.
//
// Age is computed in the host file_clock domain, NEVER mixed with
// IC_API::time() (which only decides whether a pin lasts). Bounded by
// g_cleanup_max_files_per_run; if we hit the cap, the next tick continues
// from the start of the directory tree (bounded repeated scanning, not
// cursor-based — acceptable for v1).
namespace {

struct cleanup_counts_ {
  uint64_t examined = 0, deleted = 0, failed = 0;
};

// Cleans the files of `dir`, and with `descend` also those of its
// sub-directories (one level).
void clean_dir_(const std::filesystem::path &dir, bool descend, uint64_t now_ns,
                cleanup_counts_ &n) {
  using std::chrono::duration_cast;
  using std::chrono::file_clock;
  using std::chrono::nanoseconds;

  std::error_code ec_dir;
  std::filesystem::directory_iterator it(dir, ec_dir);
  if (ec_dir) return;

  const auto end = std::filesystem::directory_iterator{};
  for (; it != end && n.examined < g_cleanup_max_files_per_run;) {
    const auto file_path = it->path();

    std::error_code ec_ftype;
    if (descend && it->is_directory(ec_ftype) && !ec_ftype) {
      clean_dir_(file_path, false, now_ns, n);
    } else if (it->is_regular_file(ec_ftype) && !ec_ftype &&
               !decision_state_pinned(file_path.string(), now_ns)) {
      ++n.examined;

      std::error_code ec_mtime;
      auto mtime = std::filesystem::last_write_time(file_path, ec_mtime);
      if (ec_mtime) {
        ++n.failed;
      } else {
        // Age in nanoseconds, computed in file_clock domain.
        // Pattern mirrors src/files.cpp:276-281.
        auto now_tp = file_clock::now();
        auto age_ns = duration_cast<nanoseconds>(now_tp.time_since_epoch() -
                                                 mtime.time_since_epoch())
                          .count();
        bool stale =
            (age_ns >= 0) && static_cast<uint64_t>(age_ns) >= g_cleanup_ttl_ns;

        if (stale) {
          std::error_code ec_rm;
          if (!std::filesystem::remove(file_path, ec_rm) || ec_rm) {
            ++n.failed;
          } else {
            ++n.deleted;
            // Pair the file-delete with metadata-delete. A false return means
            // no metadata record matched (fine if upload tracking did not
            // record this file). Best-effort: we do not escalate "no record"
            // to ++failed.
            (void)delete_file_metadata(file_path.string());
          }
        }
      }
    }

    std::error_code adv_ec;
    it.increment(adv_ec);
    if (adv_ec) break;
  }
}

} // namespace

void run_cache_cleanup_body() {
  cleanup_counts_ n;
  const uint64_t now_ns = IC_API::time();

  std::error_code ec_outer;
  std::filesystem::directory_iterator outer(".canister_cache", ec_outer);
  if (ec_outer) {
    // No .canister_cache yet — fresh canister, first run.
    ++g_cleanup_runs;
    g_cleanup_files_examined = 0;
    g_cleanup_files_deleted = 0;
    g_cleanup_files_failed = 0;
    g_cleanup_last_run_ns = now_ns;
    std::string msg = "no .canister_cache directory yet — nothing to clean";
    std::cout << "llama_cpp: " << std::string(__func__) << " - " << msg
              << std::endl;
    return;
  }

  const auto outer_end = std::filesystem::directory_iterator{};
  for (; outer != outer_end && n.examined < g_cleanup_max_files_per_run;) {
    std::error_code ec;
    if (outer->is_directory(ec) && !ec) {
      clean_dir_(outer->path() / "sessions", true, now_ns, n);
    }
    std::error_code adv_ec;
    outer.increment(adv_ec);
    if (adv_ec) break;
  }

  ++g_cleanup_runs;
  // Last-run stats: assign, do not accumulate. Each invocation overwrites
  // the previous run's numbers; callers reading the stats record see the
  // most recent run's outcome.
  g_cleanup_files_examined = n.examined;
  g_cleanup_files_deleted = n.deleted;
  g_cleanup_files_failed = n.failed;
  g_cleanup_last_run_ns = now_ns; // reporting only, not for age math.

  std::string msg = "run #" + std::to_string(g_cleanup_runs) +
                    ": examined=" + std::to_string(n.examined) +
                    " deleted=" + std::to_string(n.deleted) +
                    " failed=" + std::to_string(n.failed) +
                    " ttl_s=" + std::to_string(g_cleanup_ttl_ns / NS_PER_SEC) +
                    " cap=" + std::to_string(g_cleanup_max_files_per_run);
  std::cout << "llama_cpp: " << std::string(__func__) << " - " << msg
            << std::endl;
}

void cache_cleanup_rearm() {
  if (g_cleanup_timer_id != 0) {
    IC_API::cancel_timer(g_cleanup_timer_id);
  }
  arm_timer_();
}

// --- Endpoints ------------------------------------------------------------

void cache_cleanup_start_timer() {
  IC_API ic_api(CanisterUpdate{std::string(__func__)}, false);
  if (!has_admin_update_role(ic_api)) {
    send_access_denied_api_error(ic_api);
    return;
  }
  ic_api.from_wire();

  // Idempotent: cancel any existing armed timer before re-arming.
  cache_cleanup_rearm();
  save_settings(); // re-armed after an upgrade

  std::string msg = "timer armed; period_seconds=" +
                    std::to_string(g_cleanup_period_ns / NS_PER_SEC);
  std::cout << "llama_cpp: " << std::string(__func__) << " - " << msg
            << std::endl;

  ic_api.to_wire(
      CandidTypeVariant{"Ok", CandidTypeRecord{build_action_record_()}});
}

void cache_cleanup_stop_timer() {
  IC_API ic_api(CanisterUpdate{std::string(__func__)}, false);
  if (!has_admin_update_role(ic_api)) {
    send_access_denied_api_error(ic_api);
    return;
  }
  ic_api.from_wire();

  if (g_cleanup_timer_id != 0) {
    IC_API::cancel_timer(g_cleanup_timer_id);
    g_cleanup_timer_id = 0;
    save_settings();
    std::string msg = "timer cancelled";
    std::cout << "llama_cpp: " << std::string(__func__) << " - " << msg
              << std::endl;
  }

  ic_api.to_wire(
      CandidTypeVariant{"Ok", CandidTypeRecord{build_action_record_()}});
}

void cache_cleanup_now() {
  IC_API ic_api(CanisterUpdate{std::string(__func__)}, false);
  if (!has_admin_update_role(ic_api)) {
    send_access_denied_api_error(ic_api);
    return;
  }
  ic_api.from_wire();

  std::string msg = "manual cleanup triggered";
  std::cout << "llama_cpp: " << std::string(__func__) << " - " << msg
            << std::endl;

  run_cache_cleanup_body();

  ic_api.to_wire(
      CandidTypeVariant{"Ok", CandidTypeRecord{build_stats_record_()}});
}

void get_cache_cleanup_stats() {
  IC_API ic_api(CanisterQuery{std::string(__func__)}, false);
  if (!has_admin_query_role(ic_api)) {
    send_access_denied_api_error(ic_api);
    return;
  }
  ic_api.from_wire();

  ic_api.to_wire(
      CandidTypeVariant{"Ok", CandidTypeRecord{build_stats_record_()}});
}

void set_cache_cleanup_config() {
  IC_API ic_api(CanisterUpdate{std::string(__func__)}, false);
  if (!has_admin_update_role(ic_api)) {
    send_access_denied_api_error(ic_api);
    return;
  }

  // CacheCleanupConfigInput is a record of three opt nat64 fields:
  //   period_seconds   : opt nat64
  //   ttl_seconds      : opt nat64
  //   max_files_per_run: opt nat64
  // Each opt:
  //   - null       → no change
  //   - opt N (>0) → applied (with bounds-clamp for max_files_per_run)
  //   - opt 0      → field-specific:
  //       * period_seconds   : silently rejected (would fire forever)
  //       * ttl_seconds      : valid ("delete everything", used by tests)
  //       * max_files_per_run: clamped to floor (1)
  std::optional<uint64_t> opt_period_seconds;
  std::optional<uint64_t> opt_ttl_seconds;
  std::optional<uint64_t> opt_max_files;
  std::optional<uint64_t> opt_max_decision_states;

  CandidTypeRecord r_in;
  r_in.append("period_seconds", CandidTypeOptNat64{&opt_period_seconds});
  r_in.append("ttl_seconds", CandidTypeOptNat64{&opt_ttl_seconds});
  r_in.append("max_files_per_run", CandidTypeOptNat64{&opt_max_files});
  r_in.append("max_decision_states",
              CandidTypeOptNat64{&opt_max_decision_states});
  ic_api.from_wire(r_in);

  // period_seconds: must be > 0; otherwise silently preserve.
  if (opt_period_seconds.has_value() && *opt_period_seconds > 0 &&
      *opt_period_seconds <= MAX_SECONDS) {
    g_cleanup_period_ns = *opt_period_seconds * NS_PER_SEC;
  }
  // ttl_seconds: 0 is a valid "delete everything" value. Clamp by MAX_SECONDS.
  if (opt_ttl_seconds.has_value() && *opt_ttl_seconds <= MAX_SECONDS) {
    g_cleanup_ttl_ns = *opt_ttl_seconds * NS_PER_SEC;
  }
  // max_files_per_run: clamp to [floor, ceiling].
  if (opt_max_files.has_value()) {
    uint64_t v = *opt_max_files;
    if (v < MAX_FILES_PER_RUN_FLOOR)
      g_cleanup_max_files_per_run = MAX_FILES_PER_RUN_FLOOR;
    else if (v > MAX_FILES_PER_RUN_CEILING)
      g_cleanup_max_files_per_run = MAX_FILES_PER_RUN_CEILING;
    else g_cleanup_max_files_per_run = v;
  }

  // max_decision_states: 0 = no limit
  if (opt_max_decision_states.has_value()) {
    g_max_decision_states = *opt_max_decision_states;
  }

  // If currently armed, transparently re-arm with the new period.
  if (g_cleanup_timer_id != 0) {
    cache_cleanup_rearm();
  }
  save_settings();

  std::string msg =
      "config updated; period_seconds=" +
      std::to_string(g_cleanup_period_ns / NS_PER_SEC) +
      " ttl_seconds=" + std::to_string(g_cleanup_ttl_ns / NS_PER_SEC) +
      " max_files_per_run=" + std::to_string(g_cleanup_max_files_per_run) +
      " max_decision_states=" + std::to_string(g_max_decision_states) +
      " is_running=" + (g_cleanup_timer_id != 0 ? "true" : "false");
  std::cout << "llama_cpp: " << std::string(__func__) << " - " << msg
            << std::endl;

  ic_api.to_wire(
      CandidTypeVariant{"Ok", CandidTypeRecord{build_config_record_()}});
}
