#pragma once

#include "wasm_symbol.h"
#include <string>

bool get_canister_path_session(const std::string &path_session,
                               const std::string &principal_id,
                               std::string &canister_path_session,
                               std::string &error_msg);

// --- prompt-cache format versioning -----------------------------------------
//
// llama.cpp writes prompt-cache (session) files itself and validates them with
// LLAMA_SESSION_MAGIC + LLAMA_SESSION_VERSION. That check is NOT sufficient
// across a llama.cpp upgrade: b4531 and b10076 both use magic 'ggsn' and
// version 9, yet b10076's KV-cache serialization gained a per-stream dimension
// (llama_kv_cache::state_read_meta now takes a `strm` argument and a slot_info).
// A cache written by the old build therefore PASSES llama.cpp's check and is
// then misparsed -- silently, with no error.
//
// So we stamp each cache with our own format generation, in a sidecar file
// "<cache>.icppfmt", and discard any cache that is unstamped or mismatched.
// Bump PROMPT_CACHE_FORMAT whenever a llama.cpp upgrade changes the session
// serialization.

bool prompt_cache_format_is_current(const std::string &canister_path_session);
void prompt_cache_write_format_stamp(const std::string &canister_path_session);

// If a cache exists but was not written by this build, delete it (and its
// sidecar) so a fresh one is created. Returns true if something was discarded;
// `msg` describes what happened.
bool prompt_cache_discard_if_stale(const std::string &canister_path_session,
                                   std::string &msg);
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
