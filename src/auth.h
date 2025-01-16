#pragma once

#include "wasm_symbol.h"

#include "ic_api.h"
#include <string>

void set_access() WASM_SYMBOL_EXPORTED("canister_update set_access");
void get_access() WASM_SYMBOL_EXPORTED("canister_query get_access");

bool is_caller_a_controller(IC_API &ic_api, bool err_to_wire = true);
bool is_caller_whitelisted(IC_API &ic_api, bool err_to_wire = true);