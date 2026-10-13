// Settings that survive an upgrade.
//
// An upgrade wipes the wasm heap, but files survive it (ic-wasi-polyfill
// keeps them in stable memory). So the settings an operator makes are written
// to one file on every change, and canister_post_upgrade restores them:
//   - the access level (set_access) and the admin roles (assignAdminRole)
//   - the prompt-cache cleanup config and whether its timer runs
//     (set_cache_cleanup_config, cache_cleanup_start_timer)
// A canister upgraded from a version without the file keeps the defaults.
#pragma once

#include "wasm_symbol.h"

#include <string>

void canister_post_upgrade() WASM_SYMBOL_EXPORTED("canister_post_upgrade");

extern const char *SETTINGS_PATH;

// Writes the settings file; traps on failure, so that the change that called
// it is rolled back rather than lost on the next upgrade.
void save_settings();

// Restores the settings from the file. Returns a description of what was
// restored, or why nothing was.
std::string load_settings();
