#pragma once

#include "wasm_symbol.h"

void reset_model() WASM_SYMBOL_EXPORTED("canister_update reset_model");

void file_upload_chunk()
    WASM_SYMBOL_EXPORTED("canister_update file_upload_chunk");
