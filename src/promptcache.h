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
// The same applies to the MODEL: a cache is only valid for the model that
// wrote it. llama_context::state_read_data compares the serialized arch with
// the loaded model and THROWS on a mismatch ("wrong model arch: 'llama'
// instead of 'qwen3'"), and in this canister a throw is a trap -- which also
// leaves the offending cache in place, so every later call re-traps.
//
// So we stamp each cache with our own format generation AND the model it was
// written with, in a sidecar file "<cache>.icppfmt" (line 1 = format,
// line 2 = model description), and discard any cache that is unstamped or
// mismatched. Bump PROMPT_CACHE_FORMAT whenever a llama.cpp upgrade changes
// the session serialization.

// Description of the currently loaded model, e.g. "qwen3 1.7B Q4_K_M".
// Empty string when no model is loaded.
std::string prompt_cache_model_id();

bool prompt_cache_format_is_current(const std::string &canister_path_session);
void prompt_cache_write_format_stamp(const std::string &canister_path_session);

// Drop the stamp, so the cache counts as unstamped and is discarded on next
// use. MUST be called by anything that removes or overwrites the cache bytes
// (remove_prompt_cache, upload_prompt_cache_chunk): a stamp that outlives the
// file it describes vouches for content this build never wrote, and llama.cpp
// then traps on it.
void prompt_cache_remove_stamp(const std::string &canister_path_session);

// Carry the stamp along when the cache BYTES are copied (copy_prompt_cache).
// The copied cache really was written by this build with this model, so it
// stays valid — without this, save/restore silently degrades to a cold start.
void prompt_cache_copy_stamp(const std::string &from_session,
                             const std::string &to_session);

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
