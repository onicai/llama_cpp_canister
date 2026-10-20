#pragma once

#include "wasm_symbol.h"
#include <string>

void new_chat() WASM_SYMBOL_EXPORTED("canister_update new_chat");
void run_query() WASM_SYMBOL_EXPORTED("canister_query run_query");
void run_update() WASM_SYMBOL_EXPORTED("canister_update run_update");
void run_update_full() WASM_SYMBOL_EXPORTED("canister_update run_update_full");

// Whether a run_update_full holds the session `key` (see utf8_session_key).
// Nothing else may change that session's files then.
bool session_busy(const std::string &key);

inline const std::string SESSION_BUSY_ERROR =
    "a run_update_full is running on this prompt-cache.";

// Whether a run_update_full is in flight: the model must not change then.
bool full_call_running();
