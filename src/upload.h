#pragma once

#include "wasm_symbol.h"

void file_upload_chunk()
    WASM_SYMBOL_EXPORTED("canister_update file_upload_chunk");
void uploaded_file_details()
    WASM_SYMBOL_EXPORTED("canister_query uploaded_file_details");
