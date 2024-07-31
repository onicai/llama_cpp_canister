#pragma once

#include "ic_api.h"
#include "wasm_symbol.h"
#include <string>

extern bool ready_for_inference;

void ready() WASM_SYMBOL_EXPORTED("canister_query ready");