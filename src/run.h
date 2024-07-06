#pragma once

#include "wasm_symbol.h"

void run_query() WASM_SYMBOL_EXPORTED("canister_query run_query");
void run_update() WASM_SYMBOL_EXPORTED("canister_query run_update");