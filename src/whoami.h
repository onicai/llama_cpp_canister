#pragma once

#include "ic_api.h"
#include "wasm_symbol.h"
#include <string>

void whoami() WASM_SYMBOL_EXPORTED("canister_query whoami");