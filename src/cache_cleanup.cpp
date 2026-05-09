// Recurring prompt-cache cleanup timer — implementation.
// See cache_cleanup.h for the high-level contract.

#include "cache_cleanup.h"

#include "auth.h"
#include "ic_api.h"
#include "upload.h"

#include <chrono>
#include <cstdint>
#include <filesystem>
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

namespace {

// Re-arm the recurring timer with the current g_cleanup_period_ns.
// Caller must have first cancelled any existing timer (id stored in
// g_cleanup_timer_id).
void arm_timer_() {
  g_cleanup_timer_id = IC_API::set_timer_recurring(
      g_cleanup_period_ns, []() { run_cache_cleanup_body(); });
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
  return r;
}

CandidTypeRecord build_config_record_() {
  CandidTypeRecord r;
  r.append("period_seconds", CandidTypeNat64{g_cleanup_period_ns / NS_PER_SEC});
  r.append("ttl_seconds", CandidTypeNat64{g_cleanup_ttl_ns / NS_PER_SEC});
  r.append("max_files_per_run", CandidTypeNat64{g_cleanup_max_files_per_run});
  r.append("is_running", CandidTypeBool{g_cleanup_timer_id != 0});
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
// Walks `.canister_cache/<principal>/sessions/` using two explicit levels of
// non-recursive directory_iterator: outer over principal subdirectories,
// inner over each principal's sessions/ files. The two-level walk maps 1:1
// to the on-disk layout and is structurally clearer than a recursive walk
// with a parent-dir-name filter.
//
// Age is computed in the host file_clock domain, NEVER mixed with
// IC_API::time(). Bounded by g_cleanup_max_files_per_run; if we hit the
// cap, the next tick continues from the start of the directory tree
// (bounded repeated scanning, not cursor-based — acceptable for v1).
void run_cache_cleanup_body() {
  using std::chrono::duration_cast;
  using std::chrono::file_clock;
  using std::chrono::nanoseconds;

  uint64_t examined = 0, deleted = 0, failed = 0;

  std::error_code ec_outer;
  std::filesystem::directory_iterator outer(".canister_cache", ec_outer);
  if (ec_outer) {
    // No .canister_cache yet — fresh canister, first run.
    ++g_cleanup_runs;
    g_cleanup_files_examined = 0;
    g_cleanup_files_deleted = 0;
    g_cleanup_files_failed = 0;
    g_cleanup_last_run_ns = IC_API::time();
    return;
  }

  const auto outer_end = std::filesystem::directory_iterator{};
  for (; outer != outer_end && examined < g_cleanup_max_files_per_run;) {
    const auto principal_path = outer->path();

    std::error_code ec;
    bool is_dir = outer->is_directory(ec);
    if (ec || !is_dir) {
      std::error_code adv_ec;
      outer.increment(adv_ec);
      if (adv_ec) break;
      continue;
    }

    auto sessions_path = principal_path / "sessions";
    std::error_code sess_ec;
    if (!std::filesystem::is_directory(sessions_path, sess_ec) || sess_ec) {
      std::error_code adv_ec;
      outer.increment(adv_ec);
      if (adv_ec) break;
      continue;
    }

    std::error_code ec_inner;
    std::filesystem::directory_iterator inner(sessions_path, ec_inner);
    if (ec_inner) {
      std::error_code adv_ec;
      outer.increment(adv_ec);
      if (adv_ec) break;
      continue;
    }

    const auto inner_end = std::filesystem::directory_iterator{};
    for (; inner != inner_end && examined < g_cleanup_max_files_per_run;) {
      const auto file_path = inner->path();

      std::error_code ec_ftype;
      bool is_file = inner->is_regular_file(ec_ftype);
      if (ec_ftype || !is_file) {
        std::error_code adv_ec;
        inner.increment(adv_ec);
        if (adv_ec) break;
        continue;
      }

      ++examined;

      std::error_code ec_mtime;
      auto mtime = std::filesystem::last_write_time(file_path, ec_mtime);
      if (ec_mtime) {
        ++failed;
        std::error_code adv_ec;
        inner.increment(adv_ec);
        if (adv_ec) break;
        continue;
      }

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
          ++failed;
        } else {
          ++deleted;
          // Pair the file-delete with metadata-delete. A false return means
          // no metadata record matched (fine if upload tracking did not
          // record this file). Best-effort: we do not escalate "no record"
          // to ++failed.
          (void)delete_file_metadata(file_path.string());
        }
      }

      std::error_code adv_ec;
      inner.increment(adv_ec);
      if (adv_ec) break;
    }

    std::error_code adv_ec;
    outer.increment(adv_ec);
    if (adv_ec) break;
  }

  ++g_cleanup_runs;
  // Last-run stats: assign, do not accumulate. Each invocation overwrites
  // the previous run's numbers; callers reading the stats record see the
  // most recent run's outcome.
  g_cleanup_files_examined = examined;
  g_cleanup_files_deleted = deleted;
  g_cleanup_files_failed = failed;
  g_cleanup_last_run_ns = IC_API::time(); // reporting only, not for age math.
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
  if (g_cleanup_timer_id != 0) {
    IC_API::cancel_timer(g_cleanup_timer_id);
  }
  arm_timer_();

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

  CandidTypeRecord r_in;
  r_in.append("period_seconds", CandidTypeOptNat64{&opt_period_seconds});
  r_in.append("ttl_seconds", CandidTypeOptNat64{&opt_ttl_seconds});
  r_in.append("max_files_per_run", CandidTypeOptNat64{&opt_max_files});
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

  // If currently armed, transparently re-arm with the new period.
  if (g_cleanup_timer_id != 0) {
    IC_API::cancel_timer(g_cleanup_timer_id);
    arm_timer_();
  }

  ic_api.to_wire(
      CandidTypeVariant{"Ok", CandidTypeRecord{build_config_record_()}});
}
