#pragma once

#include "wasm_symbol.h"
#include <string>

bool get_canister_path_session(const std::string &path_session,
                               const std::string &principal_id,
                               std::string &canister_path_session,
                               std::string &error_msg);
void remove_prompt_cache()
    WASM_SYMBOL_EXPORTED("canister_update remove_prompt_cache");
void copy_prompt_cache()
    WASM_SYMBOL_EXPORTED("canister_update copy_prompt_cache");

void download_prompt_cache_chunk()
    WASM_SYMBOL_EXPORTED("canister_query download_prompt_cache_chunk");
void upload_prompt_cache_chunk()
    WASM_SYMBOL_EXPORTED("canister_update upload_prompt_cache_chunk");
void uploaded_prompt_cache_details()
    WASM_SYMBOL_EXPORTED("canister_query uploaded_prompt_cache_details");
