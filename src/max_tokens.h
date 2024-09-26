#pragma once

#include "ic_api.h"
#include "wasm_symbol.h"
#include <string>

void set_max_tokens() WASM_SYMBOL_EXPORTED("canister_update set_max_tokens");
void get_max_tokens() WASM_SYMBOL_EXPORTED("canister_query get_max_tokens");

extern uint64_t max_tokens_update;
extern uint64_t max_tokens_query;