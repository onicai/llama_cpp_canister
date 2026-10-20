#pragma once

#include "ic_api.h"
#include "wasm_symbol.h"
#include <cstdint>
#include <string>

void new_chat() WASM_SYMBOL_EXPORTED("canister_update new_chat");
void run_query() WASM_SYMBOL_EXPORTED("canister_query run_query");
void run_update() WASM_SYMBOL_EXPORTED("canister_update run_update");
void run_update_full() WASM_SYMBOL_EXPORTED("canister_update run_update_full");

// Whether a run_update_full holds the session `key` (see utf8_session_key).
// Nothing else may change that session's files then: the caller refuses, and
// this logs that `caller` was refused.
bool session_busy(const std::string &key, const std::string &caller);

inline const std::string SESSION_BUSY_ERROR =
    "a run_update_full is running on this prompt-cache.";

// Whether a run_update_full or run_decision_full is in flight: the model must
// not change then. The caller refuses, and this logs that `caller` was refused.
bool full_call_running(const std::string &caller);

// Replies 409 and returns true when full_call_running(caller).
bool reject_model_change_if_busy(IC_API &ic_api, const std::string &caller);

// Holds `key` for a call that yields to itself, while it is in scope: a
// run_update_full holds its session, a run_decision_full a key of its own.
struct SessionBusy {
  std::string key;
  uint64_t owner;
  explicit SessionBusy(const std::string &k);
  SessionBusy(const SessionBusy &) = delete;
  SessionBusy &operator=(const SessionBusy &) = delete;
  // Marks progress, so a call that is still running does not expire. False
  // when the entry expired, and this call no longer holds `key`.
  bool touch();
  // Releases `key`, unless it expired and another call holds it now.
  ~SessionBusy();
};
