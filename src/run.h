#pragma once

#include "wasm_symbol.h"
#include <string>

void new_chat() WASM_SYMBOL_EXPORTED("canister_update new_chat");
void run_query() WASM_SYMBOL_EXPORTED("canister_query run_query");
void run_update() WASM_SYMBOL_EXPORTED("canister_update run_update");