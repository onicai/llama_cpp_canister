#pragma once

#include <cstdint>
#include <fstream>
#include <system_error>

#include "ic_api.h"

// Security limits to prevent unbounded memory allocation from user input
const uint64_t MAX_CHUNK_SIZE = 2 * 1024 * 1024; // ICP message size limit
const uint64_t MAX_FILENAME_SIZE = 4096;         // Linux PATH_MAX
const uint64_t MAX_SHA256_SIZE = 64;             // SHA256 hex string length

bool open_ifstream(const std::string &f_name,
                   const std::ios_base::openmode &mode,
                   std::ifstream &if_stream, std::string &msg);
bool open_ofstream(const std::string &f_name,
                   const std::ios_base::openmode &mode,
                   std::ofstream &if_stream, std::string &msg);

std::tuple<int, std::vector<char *>, std::unique_ptr<std::vector<std::string>>>
get_args_for_main(IC_API &ic_api);

bool my_create_directory(const std::filesystem::path &dir_path,
                         std::string &error_msg);

void send_output_record_result_error_to_wire(IC_API &ic_api,
                                             uint16_t http_status_code,
                                             const std::string &error_msg);

// ---------------------------------------------------------------------------
// UTF-8 safety for everything we put into a Candid `text` field.
//
// run_update returns generation in max_tokens-sized chunks, and byte-level BPE
// encodes scripts like Devanagari as byte tokens, so a chunk boundary lands in
// the middle of a multi-byte codepoint. Candid `text` MUST be valid UTF-8: a
// strict decoder (Motoko RTS, the Rust candid crate that icp-cli uses) traps
// while decoding the reply, which kills the CALLING canister - and in Motoko
// that trap is not catchable. See TMP-HANDOVER-hindi-utf8-chunking.md.
//
// Both are implemented on common_parse_utf8_codepoint() from the vendored fork
// (common/unicode.cpp, already compiled in). Do NOT use src/unicode.h's
// unicode_cpt_from_utf8(): it throws on malformed input, and a throw TRAPS here
// (see src/wasi-exception-stubs.cpp) - it would kill the canister on exactly the
// input these helpers exist to repair.

// Length of the longest prefix of `s` that does not end mid-codepoint. Use it to
// split generated output: send the prefix, carry the remainder (0-3 bytes) into
// the next call so no bytes are lost. Invalid bytes in the middle are stepped
// over rather than treated as a boundary, so this always makes progress.
size_t utf8_valid_prefix_len(const std::string &s);

// Lossy: every invalid or truncated sequence becomes U+FFFD. For informational
// fields that are rebuilt each call and so cannot be carried.
std::string utf8_sanitize(const std::string &s);

// Per-session carry of a split codepoint's trailing bytes, keyed on the
// principal-qualified prompt-cache path. It lives here rather than in run.cpp so
// promptcache.cpp can clear it when a cache is removed or replaced.
//
// It MUST survive between update calls, so - unlike the statics in main_.cpp - it
// is deliberately NOT reset by reset_static_memory(). Do not add it there. It
// holds at most 3 bytes per session, is flushed on EOG, and is cleared by
// new_chat / remove_prompt_cache. Losing it on upgrade costs at most 3 bytes of
// one in-flight generation.
std::string utf8_carry_get(const std::string &session_key);
void utf8_carry_set(const std::string &session_key, const std::string &tail);
void utf8_carry_clear(const std::string &session_key);