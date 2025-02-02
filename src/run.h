#pragma once

#include "wasm_symbol.h"
#include <string>

void new_chat() WASM_SYMBOL_EXPORTED("canister_update new_chat");
void run_query() WASM_SYMBOL_EXPORTED("canister_query run_query");
void run_update() WASM_SYMBOL_EXPORTED("canister_update run_update");
void remove_prompt_cache()
    WASM_SYMBOL_EXPORTED("canister_update remove_prompt_cache");
void remove_log_file() WASM_SYMBOL_EXPORTED("canister_update remove_log_file");

bool get_canister_path_session(const std::string &path_session,
                               const std::string &principal_id,
                               std::string &canister_path_session,
                               std::string &error_msg);