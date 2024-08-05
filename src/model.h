#pragma once

#include "wasm_symbol.h"
#include <string>

void reset_model() WASM_SYMBOL_EXPORTED("canister_update reset_model");
void load_model() WASM_SYMBOL_EXPORTED("canister_update load_model");