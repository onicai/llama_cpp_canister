// Settings that survive an upgrade. See settings.h.

#include "settings.h"

#include "auth.h"
#include "cache_cleanup.h"
#include "ic_api.h"

#include "common/json.h"

#include <fstream>
#include <iostream>
#include <sstream>
#include <vector>

const char *SETTINGS_PATH = ".canister_settings.json";

namespace {

const char *CLEANUP_KEYS[] = {"period_ns", "ttl_ns", "max_files_per_run",
                              "max_decision_states"};

uint64_t *cleanup_value(const std::string &key) {
  if (key == "period_ns") return &g_cleanup_period_ns;
  if (key == "ttl_ns") return &g_cleanup_ttl_ns;
  if (key == "max_files_per_run") return &g_cleanup_max_files_per_run;
  return &g_max_decision_states;
}

// common_json has no is_number_unsigned(): a negative integer is told apart
// by its text, so that get<uint64_t>() never turns it into a huge value
bool is_uint(const common_json &j, const char *key) {
  return j.contains(key) && j.at(key).is_number_integer() &&
         j.at(key).dump().rfind('-', 0) != 0;
}

bool is_string(const common_json &j, const char *key) {
  return j.contains(key) && j.at(key).is_string();
}

} // namespace

void save_settings() {
  common_json roles = common_json::array();
  for (const auto &a : get_admin_role_assignments()) {
    common_json r = common_json::object();
    r["principal"] = common_json(a.principal);
    r["role"] = common_json(a.role == AdminRole::AdminUpdate ? "AdminUpdate"
                                                             : "AdminQuery");
    r["assignedBy"] = common_json(a.assignedBy);
    r["assignedAt"] = common_json(a.assignedAt);
    r["note"] = common_json(a.note);
    roles.push_back(r);
  }
  common_json cleanup = common_json::object();
  for (const char *key : CLEANUP_KEYS) {
    cleanup[key] = common_json(*cleanup_value(key));
  }
  cleanup["running"] = common_json(g_cleanup_timer_id != 0);

  common_json j = common_json::object();
  j["access_level"] = common_json(get_access_level());
  j["admin_roles"] = roles;
  j["cache_cleanup"] = cleanup;

  std::ofstream out(SETTINGS_PATH, std::ios::trunc);
  out << j.dump();
  if (!out) {
    IC_API::trap(std::string("failed to save the settings to ") +
                 SETTINGS_PATH);
  }
}

// The file is written by this canister only; still, every value is
// type-checked because a get<T>() mismatch would throw (= trap), and a trap
// in canister_post_upgrade would make the upgrade fail.
std::string load_settings() {
  std::ifstream in(SETTINGS_PATH);
  if (!in) {
    return std::string("no ") + SETTINGS_PATH + ", the defaults apply";
  }
  std::stringstream ss;
  ss << in.rdbuf();
  const common_json j = common_json::parse_no_throw(ss.str());
  if (j.is_discarded() || !j.is_object()) {
    return std::string(SETTINGS_PATH) + " is unreadable, the defaults apply";
  }

  if (is_uint(j, "access_level") && j.at("access_level").get<uint64_t>() <= 1) {
    restore_access_level((uint16_t)j.at("access_level").get<uint64_t>());
  }

  std::vector<AdminRoleAssignment> roles;
  if (j.contains("admin_roles") && j.at("admin_roles").is_array()) {
    const common_json &arr = j.at("admin_roles");
    for (auto it = arr.begin(); it != arr.end(); ++it) {
      const common_json &r = it.value();
      if (!r.is_object() || !is_string(r, "principal") ||
          !is_string(r, "role") || !is_string(r, "assignedBy") ||
          !is_uint(r, "assignedAt") || !is_string(r, "note")) {
        continue;
      }
      AdminRoleAssignment a;
      a.principal = r.at("principal").get<std::string>();
      a.role = r.at("role").get<std::string>() == "AdminUpdate"
                   ? AdminRole::AdminUpdate
                   : AdminRole::AdminQuery;
      a.assignedBy = r.at("assignedBy").get<std::string>();
      a.assignedAt = r.at("assignedAt").get<uint64_t>();
      a.note = r.at("note").get<std::string>();
      roles.push_back(a);
    }
  }
  const size_t n_roles = roles.size();
  restore_admin_role_assignments(std::move(roles));

  bool running = false;
  if (j.contains("cache_cleanup") && j.at("cache_cleanup").is_object()) {
    const common_json &c = j.at("cache_cleanup");
    for (const char *key : CLEANUP_KEYS) {
      if (is_uint(c, key)) {
        *cleanup_value(key) = c.at(key).get<uint64_t>();
      }
    }
    running = c.contains("running") && c.at("running").is_boolean() &&
              c.at("running").get<bool>();
  }
  if (running) {
    cache_cleanup_rearm();
  }

  return "access level " + std::to_string(get_access_level()) + ", " +
         std::to_string(n_roles) + " admin role(s), cache cleanup " +
         (running ? "running" : "off") + ", max_decision_states " +
         std::to_string(g_max_decision_states);
}

void canister_post_upgrade() {
  IC_API ic_api(CanisterPostUpgrade{std::string(__func__)}, false);
  std::cout << "llama_cpp: " << std::string(__func__) << " - restored "
            << load_settings() << std::endl;
}
