#pragma once

#include "wasm_symbol.h"
#include <string>

void remove_log_file() WASM_SYMBOL_EXPORTED("canister_update remove_log_file");
void log_pause() WASM_SYMBOL_EXPORTED("canister_update log_pause");
void log_resume() WASM_SYMBOL_EXPORTED("canister_update log_resume");