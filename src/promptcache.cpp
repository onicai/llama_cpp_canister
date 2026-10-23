#include "promptcache.h"

#include "auth.h"
#include "common.h"
#include "db_chats.h"
#include "download.h"
#include "http.h"
#include "main_.h"
#include "max_tokens.h"
#include "run.h"
#include "upload.h"
#include "utils.h"

#include "arg.h"
#include "llama.h" // llama_model_desc, for the model-identity stamp
#include "log.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include "ic_api.h"

/*
The prompt cache is also called a session file
It contains the state of the LLM and is preserved between update calls

This module provides functions to manage the prompt cache file to
optimize performance & cost
*/

static void print_usage(int argc, char **argv) {
  // do nothing function
}

// --- prompt-cache format versioning -----------------------------------------
// See promptcache.h for why llama.cpp's own magic+version check is insufficient.

// Bump the vN below whenever the session layout changes in a way that
// LLAMA_SESSION_VERSION does not capture, or when the stamp file itself gains
// fields (old stamps then read as stale and their caches are discarded -- a
// one-time cold start, never a trap). llama.cpp's own session version bumps
// are picked up automatically (since v4).
//   1 = llama.cpp b4531 (6152129d) and earlier -- never actually stamped
//   2 = llama.cpp b10076 (305ba519), llama_memory_* refactor
//   3 = same serialization as 2; stamp gains a context-layout line (line 3),
//       so a re-load_model with different context flags (ctx-size, cache
//       types, ...) discards the now-unloadable cache instead of trapping in
//       llama_state_load_file, whose catch never runs on WASI
//   4 = llama.cpp b11476 (988190680): LLAMA_SESSION_VERSION 9 -> 11. The stamp
//       was NOT bumped in v0.20.0, so a cache written by v0.19.1 passed the
//       check, llama_state_load_file rejected it (version mismatch), and every
//       run_update on it failed with "failed to load session file" until the
//       cleanup timer removed it. From v4 on, the format line also carries
//       llama.cpp's own LLAMA_SESSION_VERSION and LLAMA_STATE_SEQ_VERSION, so an
//       upstream serialization bump invalidates old caches automatically.
std::string prompt_cache_format() {
  return std::string("llama_cpp_canister-prompt-cache-v4 session=") +
         std::to_string(LLAMA_SESSION_VERSION) +
         " seq=" + std::to_string(LLAMA_STATE_SEQ_VERSION);
}

// The format stamp of a cache: "<cache>.icppfmt".
static std::string
prompt_cache_stamp_path(const std::string &canister_path_session) {
  return canister_path_session + ".icppfmt";
}

// The logits sidecar of a cache: "<cache>.logits".
static std::string
prompt_cache_logits_path(const std::string &canister_path_session) {
  return canister_path_session + ".logits";
}

// Every sidecar describes the cache BYTES, so all of them are removed and
// copied together with the cache.
static const char *const PROMPT_CACHE_SIDECARS[] = {".icppfmt", ".logits"};

// Whether a canister path names a sidecar instead of a cache. Sidecars are
// written by the canister only: a stamp that arrived any other way could
// vouch for bytes this build never wrote, and llama.cpp traps on those.
static bool is_prompt_cache_sidecar(const std::string &canister_path_session) {
  const std::string name =
      std::filesystem::path(canister_path_session).lexically_normal().string();
  for (const char *suffix : PROMPT_CACHE_SIDECARS) {
    const std::string s(suffix);
    if (name.size() >= s.size() &&
        name.compare(name.size() - s.size(), s.size(), s) == 0) {
      return true;
    }
  }
  return false;
}

static std::string sidecar_name_error(const std::string &promptcache) {
  return "'" + promptcache +
         "' names a prompt-cache sidecar (.icppfmt, .logits), which only the "
         "canister writes";
}

std::string prompt_cache_model_id() {
  // Identity of the currently loaded model, e.g. "qwen3 1.7B Q4_K_M".
  // Empty when no model is loaded yet (g_model is set by main_ at load_model).
  if (g_model == nullptr || *g_model == nullptr) return "";

  char buf[256];
  buf[0] = '\0';
  // snprintf semantics: the return value is the length the description WOULD
  // have, so read the (always NUL-terminated) buffer instead of trusting it.
  llama_model_desc(*g_model, buf, sizeof(buf));
  return std::string(buf);
}

std::string prompt_cache_layout_id() {
  // Identity of the loaded context's session-file layout (ctx size, seq
  // count, flash-attn, kv-unified, KV cache types). Empty when no model is
  // loaded yet. A cache written under a different layout makes
  // llama_state_load_file THROW -- a trap on WASI -- so it must be discarded
  // instead of loaded.
  llama_context *ctx = icpp_persisted_ctx();
  if (ctx == nullptr) return "";

  char buf[128];
  buf[0] = '\0';
  llama_state_layout_desc(ctx, buf, sizeof(buf));
  return std::string(buf);
}

bool prompt_cache_format_is_current(const std::string &canister_path_session) {
  const std::string stamp_path = prompt_cache_stamp_path(canister_path_session);

  std::ifstream f(stamp_path);
  if (!f.is_open()) return false; // unstamped => written by an older build

  std::string stamp;
  std::getline(f, stamp);
  if (stamp != prompt_cache_format()) return false;

  // Second line: the model the cache was written with. A cache is only valid
  // for the model that produced it -- see prompt_cache_discard_if_stale.
  // Absent (older stamp) counts as a mismatch, so those caches are discarded.
  std::string stamped_model;
  if (!std::getline(f, stamped_model)) return false;

  const std::string current_model = prompt_cache_model_id();
  // No model loaded => nothing to compare against; do not discard on that basis.
  if (current_model.empty()) return true;

  if (stamped_model != current_model) return false;

  // Third line: the context layout the cache was written under. A re-load
  // with different context flags (e.g. --ctx-size) changes the session-file
  // byte layout, so llama_state_load_file would throw -- a trap on WASI. An
  // absent third line (an older v2 stamp) already failed the format check
  // above, so reaching here means the line is present.
  std::string stamped_layout;
  if (!std::getline(f, stamped_layout)) return false;
  const std::string current_layout = prompt_cache_layout_id();
  // No model loaded => nothing to compare against; do not discard on that basis.
  if (!current_layout.empty() && stamped_layout != current_layout) return false;

  return true;
}

// Removes the stamp and every other sidecar of a cache.
void prompt_cache_remove_stamp(const std::string &canister_path_session) {
  // Drop the sidecar so the cache counts as UNSTAMPED and will be discarded.
  // Must be called by anything that removes or overwrites the cache bytes:
  // a stamp that outlives the file it describes is worse than no stamp at all,
  // because prompt_cache_discard_if_stale() would then vouch for content this
  // build never wrote and llama.cpp would trap on it.
  if (canister_path_session.empty()) return;
  std::error_code ec;
  for (const char *suffix : PROMPT_CACHE_SIDECARS) {
    std::filesystem::remove(canister_path_session + suffix, ec);
  }
}

// Copies the stamp and every other sidecar along with the cache bytes.
void prompt_cache_copy_stamp(const std::string &from_session,
                             const std::string &to_session) {
  // The stamp describes the BYTES, so it must travel with them. A cache copied
  // from another of this caller's caches really was written by this build with
  // this model, so its stamp stays valid and the restored cache stays warm.
  // (Without this, save/restore via copy_prompt_cache silently degrades into a
  // cold start on every restore.)
  std::error_code ec;
  for (const char *suffix : PROMPT_CACHE_SIDECARS) {
    const std::string from_sidecar = from_session + suffix;
    const std::string to_sidecar = to_session + suffix;
    std::filesystem::remove(to_sidecar, ec);
    if (std::filesystem::exists(from_sidecar)) {
      std::filesystem::copy(from_sidecar, to_sidecar, ec);
    }
  }
}

void prompt_cache_write_format_stamp(const std::string &canister_path_session) {
  std::ofstream f(prompt_cache_stamp_path(canister_path_session),
                  std::ios::trunc);
  if (f.is_open()) {
    f << prompt_cache_format() << std::endl;
    f << prompt_cache_model_id() << std::endl;
    f << prompt_cache_layout_id() << std::endl;
  }
}

// --- logits sidecar ----------------------------------------------------------
// Layout of "<cache>.logits": a header, then n_vocab floats.
static const uint32_t PROMPT_CACHE_LOGITS_MAGIC = 0x474c4349; // 'ICLG'
static const uint32_t PROMPT_CACHE_LOGITS_VERSION = 1;

struct prompt_cache_logits_header {
  uint32_t magic;
  uint32_t version;
  int32_t n_vocab;
  int32_t last_token;
  uint64_t n_tokens;
};

// The vocabulary size of the context's model: the length of a logits row.
static int32_t ctx_n_vocab(llama_context *ctx) {
  return llama_vocab_n_tokens(llama_model_get_vocab(llama_get_model(ctx)));
}

// Saves the logits after session_tokens.back(); see promptcache.h.
bool prompt_cache_write_logits(const std::string &canister_path_session,
                               llama_context *ctx,
                               const std::vector<llama_token> &session_tokens) {
  const float *logits =
      session_tokens.empty() ? nullptr : llama_get_logits_ith(ctx, -1);
  if (logits == nullptr) {
    prompt_cache_remove_logits(canister_path_session);
    return false;
  }

  const prompt_cache_logits_header h{
      PROMPT_CACHE_LOGITS_MAGIC, PROMPT_CACHE_LOGITS_VERSION, ctx_n_vocab(ctx),
      session_tokens.back(), (uint64_t)session_tokens.size()};
  std::ofstream f(prompt_cache_logits_path(canister_path_session),
                  std::ios::binary | std::ios::trunc);
  if (f.is_open()) {
    f.write(reinterpret_cast<const char *>(&h), sizeof(h));
    f.write(reinterpret_cast<const char *>(logits),
            (std::streamsize)h.n_vocab * sizeof(float));
  }
  if (!f) {
    f.close();
    prompt_cache_remove_logits(canister_path_session);
    return false;
  }
  return true;
}

// Restores the saved logits into ctx after validating them; see promptcache.h.
bool prompt_cache_restore_logits(
    const std::string &canister_path_session, llama_context *ctx,
    const std::vector<llama_token> &session_tokens) {
  if (session_tokens.empty()) return false;

  std::ifstream f(prompt_cache_logits_path(canister_path_session),
                  std::ios::binary);
  if (!f.is_open()) return false;

  prompt_cache_logits_header h{};
  f.read(reinterpret_cast<char *>(&h), sizeof(h));
  if (!f || h.magic != PROMPT_CACHE_LOGITS_MAGIC ||
      h.version != PROMPT_CACHE_LOGITS_VERSION ||
      h.n_vocab != ctx_n_vocab(ctx) ||
      h.n_tokens != (uint64_t)session_tokens.size() ||
      h.last_token != session_tokens.back()) {
    return false;
  }

  std::vector<float> logits((size_t)h.n_vocab);
  f.read(reinterpret_cast<char *>(logits.data()),
         (std::streamsize)h.n_vocab * sizeof(float));
  if (!f) return false;

  return llama_set_logits_last(ctx, logits.data(), h.n_vocab);
}

// Removes the logits sidecar, so no stale logits outlive the cache bytes.
void prompt_cache_remove_logits(const std::string &canister_path_session) {
  if (canister_path_session.empty()) return;
  std::error_code ec;
  std::filesystem::remove(prompt_cache_logits_path(canister_path_session), ec);
}

// Deletes a cache (and its sidecars) not written by this build with this model.
bool prompt_cache_discard_if_stale(const std::string &canister_path_session,
                                   std::string &msg) {
  if (canister_path_session.empty()) return false;
  if (!std::filesystem::exists(canister_path_session)) return false;
  if (prompt_cache_format_is_current(canister_path_session)) return false;

  // Two ways a cache becomes unusable, both of which llama.cpp handles by
  // throwing -- and in this canister a throw is a TRAP (IC0503), which also
  // leaves the half-read cache in place so every later call re-traps:
  //
  //  (-) written by an older llama.cpp: b4531 and b10076 share magic 'ggsn'
  //      and version 9, so llama.cpp ACCEPTS the file and then misparses it.
  //  (-) written with a different model: llama_context::state_read_data
  //      compares the serialized arch against the loaded model and throws
  //      "wrong model arch: 'llama' instead of 'qwen3'". Comparing the full
  //      model description also catches same-arch/different-size swaps
  //      (e.g. Qwen3-0.6B -> Qwen3-1.7B), which the arch check alone misses.
  //
  // Either way: delete it and start cold. That is recoverable; a trap is not.
  std::error_code ec;
  std::filesystem::remove(canister_path_session, ec);
  prompt_cache_remove_stamp(canister_path_session);

  msg = "Discarded prompt-cache file that was not written by this build with "
        "the currently loaded model (" +
        prompt_cache_model_id() + "): " + canister_path_session;
  return true;
}

bool get_canister_path_session(const std::string &path_session,
                               const std::string &principal_id,
                               std::string &canister_path_session,
                               std::string &error_msg) {
  // We store the prompt cache files in a folder named with the principal id of the caller
  //
  // Note: to save multiple conversations per user, the front end can simply assign
  //       a unique prompt cache file per conversation, and that will do the job !
  //

  std::string path_session_ = path_session;
  canister_path_session = "";
  error_msg = "";

  if (!path_session_.empty()) {
    // Remove all leading '/'
    size_t pos = path_session_.find_first_not_of('/');
    if (pos != std::string::npos) {
      path_session_.erase(0, pos);
    } else {
      // If the string only contains slashes, clear it
      path_session_.clear();
    }

    // The cache file will be stored in ".cache/<principal_id>/<path_session-with_/replaced-by-_>"
    canister_path_session =
        ".canister_cache/" + principal_id + "/sessions/" + path_session_;

    // Make sure that the cache directory exists, else llama.cpp cannot create the file
    std::filesystem::path file_path(canister_path_session);
    std::filesystem::path dir_path = file_path.parent_path();
    if (!my_create_directory(dir_path, error_msg)) {
      return false;
    }
  }
  return true;
}

void remove_prompt_cache() {
  IC_API ic_api(CanisterUpdate{std::string(__func__)}, false);
  std::string error_msg;
  if (!has_admin_update_or_whitelisted(ic_api)) {
    send_access_denied_output_record(ic_api);
    return;
  }

  CandidTypePrincipal caller = ic_api.get_caller();
  std::string principal_id = caller.get_text();

  auto [argc, argv, args] = get_args_for_main(ic_api);

  // Get the cache filename from --prompt-cache in args
  common_params params;
  if (!common_params_parse(argc, argv.data(), params, LLAMA_EXAMPLE_COMPLETION,
                           print_usage)) {
    error_msg = "Cannot parse args.";
    send_output_record_result_error_to_wire(
        ic_api, Http::StatusCode::InternalServerError, error_msg);
    return;
  }

  if (session_busy(utf8_session_key(params.path_prompt_cache, principal_id),
                   __func__)) {
    send_output_record_result_error_to_wire(ic_api, Http::StatusCode::Conflict,
                                            SESSION_BUSY_ERROR);
    return;
  }

  // Each principal has their own cache folder
  std::string path_session = params.path_prompt_cache;
  std::string canister_path_session;
  if (!get_canister_path_session(path_session, principal_id,
                                 canister_path_session, error_msg)) {
    send_output_record_result_error_to_wire(
        ic_api, Http::StatusCode::InternalServerError, error_msg);
    return;
  }
  path_session = canister_path_session;

  if (is_prompt_cache_sidecar(path_session)) {
    send_output_record_result_error_to_wire(
        ic_api, Http::StatusCode::BadRequest,
        sidecar_name_error(params.path_prompt_cache));
    return;
  }

  std::string msg;
  if (!path_session.empty()) {
    // Remove the file if it exists
    if (std::filesystem::exists(path_session)) {
      bool success = std::filesystem::remove(path_session);
      // Never leave the stamp behind: it would vouch for whatever bytes appear
      // at this path next (e.g. an uploaded cache from another build/model).
      prompt_cache_remove_stamp(path_session);
      // Same reasoning for any half-codepoint we were holding for this session.
      // Key it exactly as run() does; path_session is the CANONICAL path here.
      utf8_carry_clear(
          utf8_session_key(params.path_prompt_cache, principal_id));
      if (success) {
        msg = "Cache file " + path_session + " deleted successfully";
      } else {
        error_msg = "Error deleting cache file " + path_session;
        send_output_record_result_error_to_wire(
            ic_api, Http::StatusCode::InternalServerError, error_msg);
        return;
      }
    } else {
      msg = "Cache file " + path_session + " not found. Nothing to delete.";
    }
  } else {
    error_msg = "ERROR: path_session is empty ";
    send_output_record_result_error_to_wire(
        ic_api, Http::StatusCode::InternalServerError, error_msg);
    return;
  }
  // std::cout << "llama_cpp: " << std::string(__func__) << " - " << msg << std::endl;

  // Return output over the wire
  CandidTypeRecord r_out;
  r_out.append("status_code", CandidTypeNat16{Http::StatusCode::OK}); // 200
  r_out.append("conversation", CandidTypeText{""});
  r_out.append("output", CandidTypeText{msg});
  r_out.append("error", CandidTypeText{""});
  r_out.append("prompt_remaining", CandidTypeText{""});
  r_out.append("generated_eog", CandidTypeBool{false});
  ic_api.to_wire(CandidTypeVariant{"Ok", r_out});
}

void copy_prompt_cache() {
  IC_API ic_api(CanisterUpdate{std::string(__func__)}, false);
  std::string error_msg;
  if (!has_admin_update_or_whitelisted(ic_api)) {
    send_access_denied_api_error(ic_api);
    return;
  }

  CandidTypePrincipal caller = ic_api.get_caller();
  std::string principal_id = caller.get_text();

  std::string from{""};
  std::string to{""};
  CandidTypeRecord r_in;
  r_in.append("from", CandidTypeText{&from});
  r_in.append("to", CandidTypeText{&to});
  ic_api.from_wire(r_in);

  if (session_busy(utf8_session_key(to, principal_id), __func__)) {
    ic_api.to_wire(CandidTypeVariant{
        "Err", CandidTypeVariant{"Other", CandidTypeText{SESSION_BUSY_ERROR}}});
    return;
  }

  // Each principal has their own cache folder
  std::string from_path;
  if (!get_canister_path_session(from, principal_id, from_path, error_msg)) {
    ic_api.to_wire(CandidTypeVariant{
        "Err", CandidTypeVariant{"Other", CandidTypeText{error_msg}}});
    return;
  }
  std::string to_path;
  if (!get_canister_path_session(to, principal_id, to_path, error_msg)) {
    ic_api.to_wire(CandidTypeVariant{
        "Err", CandidTypeVariant{"Other", CandidTypeText{error_msg}}});
    return;
  }

  // A copy to or from a sidecar would let a stamp vouch for other bytes
  for (const auto &[name, path] : {std::pair{from, from_path}, {to, to_path}}) {
    if (is_prompt_cache_sidecar(path)) {
      ic_api.to_wire(CandidTypeVariant{
          "Err", CandidTypeVariant{"Other",
                                   CandidTypeText{sidecar_name_error(name)}}});
      return;
    }
  }

  // copy the file from_path to to_path if it exists
  if (!std::filesystem::exists(from_path)) {
    error_msg = "File " + from_path + " does not exist.";
    ic_api.to_wire(CandidTypeVariant{
        "Err", CandidTypeVariant{"Other", CandidTypeText{error_msg}}});
    return;
  }

  // first remove the 'to' file if it already exists
  if (std::filesystem::exists(to_path)) {
    bool success = std::filesystem::remove(to_path);
    if (!success) {
      error_msg = "Could not remove existing 'to' file " + to_path;
      ic_api.to_wire(CandidTypeVariant{
          "Err", CandidTypeVariant{"Other", CandidTypeText{error_msg}}});
      return;
    }
  }

  // now copy the 'from' file to 'to' file
  std::error_code ec;
  std::filesystem::copy(from_path, to_path, ec);
  if (ec) {
    error_msg = "Error copying file from " + from_path + " to " + to_path;
    ic_api.to_wire(CandidTypeVariant{
        "Err", CandidTypeVariant{"Other", CandidTypeText{error_msg}}});
    return;
  }

  // The format/model stamp describes the bytes we just copied, so carry it
  // across. Without this the restored cache is unstamped, gets discarded on
  // first use, and the save/restore workflow silently loses its warm cache.
  prompt_cache_copy_stamp(from_path, to_path);

  CandidTypeRecord status_code_record;
  status_code_record.append("status_code", CandidTypeNat16{200});
  ic_api.to_wire(CandidTypeVariant{"Ok", status_code_record});
}

void download_prompt_cache_chunk() {
  IC_API ic_api(CanisterQuery{std::string(__func__)}, false);
  if (!has_admin_query_role(ic_api)) {
    send_access_denied_api_error(ic_api);
    return;
  }

  CandidTypePrincipal caller = ic_api.get_caller();
  std::string principal_id = caller.get_text();

  // Get filename to download and the chunksize
  std::string promptcache{""};
  uint64_t chunksize{0};
  uint64_t offset{0};

  CandidTypeRecord r_in;
  r_in.append("promptcache", CandidTypeText{&promptcache});
  r_in.append("chunksize", CandidTypeNat64{&chunksize});
  r_in.append("offset", CandidTypeNat64{&offset});
  ic_api.from_wire(r_in);

  // Each principal has their own cache folder
  std::string filename;
  std::string error_msg;
  if (!get_canister_path_session(promptcache, principal_id, filename,
                                 error_msg)) {
    ic_api.to_wire(CandidTypeVariant{
        "Err", CandidTypeVariant{"Other", CandidTypeText{error_msg}}});
    return;
  }

  file_download_chunk_(ic_api, filename, chunksize, offset);
}

void upload_prompt_cache_chunk() {
  IC_API ic_api(CanisterUpdate{std::string(__func__)}, false);
  if (!has_admin_update_role(ic_api)) {
    send_access_denied_api_error(ic_api);
    return;
  }

  CandidTypePrincipal caller = ic_api.get_caller();
  std::string principal_id = caller.get_text();

  // Get filename and the chunk to write to it
  std::string promptcache{""};
  std::vector<uint8_t> v;
  uint64_t chunksize{0};
  uint64_t offset{0};

  CandidTypeRecord r_in;
  r_in.append("promptcache", CandidTypeText{&promptcache});
  r_in.append("chunk", CandidTypeVecNat8{&v});
  r_in.append("chunksize", CandidTypeNat64{&chunksize});
  r_in.append("offset", CandidTypeNat64{&offset});
  ic_api.from_wire(r_in);

  if (session_busy(utf8_session_key(promptcache, principal_id), __func__)) {
    ic_api.to_wire(CandidTypeVariant{
        "Err", CandidTypeVariant{"Other", CandidTypeText{SESSION_BUSY_ERROR}}});
    return;
  }

  // Each principal has their own cache folder
  std::string filename;
  std::string error_msg;
  if (!get_canister_path_session(promptcache, principal_id, filename,
                                 error_msg)) {
    ic_api.to_wire(CandidTypeVariant{
        "Err", CandidTypeVariant{"Other", CandidTypeText{error_msg}}});
    return;
  }

  if (is_prompt_cache_sidecar(filename)) {
    ic_api.to_wire(CandidTypeVariant{
        "Err", CandidTypeVariant{
                   "Other", CandidTypeText{sidecar_name_error(promptcache)}}});
    return;
  }

  // The uploaded bytes did not come from this canister, so any existing stamp
  // no longer describes this file. Drop it: the cache is then treated as
  // unstamped and discarded on first use (a cold start), instead of being
  // handed to llama.cpp, which traps on a session it cannot parse. Only
  // stamp_uploaded_prompt_cache stamps it again, after validating it.
  prompt_cache_remove_stamp(filename);

  file_upload_chunk_(ic_api, filename, v, chunksize, offset);
}

void uploaded_prompt_cache_details() {
  // Returns the metadata for an uploaded prompt cache

  IC_API ic_api(CanisterQuery{std::string(__func__)}, false);
  if (!has_admin_query_role(ic_api)) {
    send_access_denied_api_error(ic_api);
    return;
  }

  CandidTypePrincipal caller = ic_api.get_caller();
  std::string principal_id = caller.get_text();

  // Get filename
  std::string promptcache{""};

  CandidTypeRecord r_in;
  r_in.append("promptcache", CandidTypeText{&promptcache});
  ic_api.from_wire(r_in);

  // Each principal has their own cache folder
  std::string filename;
  std::string error_msg;
  if (!get_canister_path_session(promptcache, principal_id, filename,
                                 error_msg)) {
    ic_api.to_wire(CandidTypeVariant{
        "Err", CandidTypeVariant{"Other", CandidTypeText{error_msg}}});
    return;
  }

  uploaded_file_details_(ic_api, filename);
}

// --- restoring an uploaded prompt cache --------------------------------------

// Checks the head of a session file without llama.cpp, which throws (a trap in
// a canister) on many mismatches. Layout, from llama_context::state_save_file:
//   u32 magic, u32 version, u32 n_token_count, i32 tokens[n_token_count],
//   u32 arch length, arch name (state_write_data), then the memory module.
// Returns "" when the head matches the loaded model and context.
static std::string prompt_cache_header_error(const std::string &path,
                                             llama_context *ctx,
                                             const std::string &arch) {
  std::error_code ec;
  const uint64_t file_size = std::filesystem::file_size(path, ec);
  if (ec) return "cannot read the size of " + path;

  std::ifstream f(path, std::ios::binary);
  if (!f.is_open()) return "cannot open " + path;

  uint32_t head[3] = {0, 0, 0};
  f.read(reinterpret_cast<char *>(head), sizeof(head));
  if (!f) return "the file is shorter than a session file header";
  const uint32_t magic = head[0];
  const uint32_t version = head[1];
  const uint64_t n_tokens = head[2];
  if (magic != LLAMA_SESSION_MAGIC) {
    return "not a session file (magic " + std::to_string(magic) +
           ", expected " + std::to_string(LLAMA_SESSION_MAGIC) + ")";
  }
  if (version != LLAMA_SESSION_VERSION) {
    return "session version " + std::to_string(version) + ", expected " +
           std::to_string(LLAMA_SESSION_VERSION);
  }
  const uint64_t n_ctx = llama_n_ctx(ctx);
  if (n_tokens > n_ctx) {
    return "the session holds " + std::to_string(n_tokens) +
           " tokens, more than the context size " + std::to_string(n_ctx);
  }
  // n_tokens <= n_ctx, so this cannot overflow
  const uint64_t n_head = sizeof(head) + 4 * n_tokens + 4 + arch.size();
  if (file_size < n_head) {
    return "the file has " + std::to_string(file_size) +
           " bytes, fewer than the " + std::to_string(n_head) +
           " of its header";
  }

  // Every token must be in the vocabulary. Read in fixed-size blocks, so the
  // header cannot make this allocate more than one block.
  const int32_t n_vocab = ctx_n_vocab(ctx);
  std::vector<llama_token> block(4096);
  for (uint64_t done = 0; done < n_tokens;) {
    const uint64_t n = std::min<uint64_t>(block.size(), n_tokens - done);
    f.read(reinterpret_cast<char *>(block.data()),
           (std::streamsize)(n * sizeof(llama_token)));
    if (!f) return "cannot read the tokens of the session";
    for (uint64_t i = 0; i < n; ++i) {
      if (block[i] < 0 || block[i] >= n_vocab) {
        return "token " + std::to_string(done + i) + " is " +
               std::to_string(block[i]) + ", outside the vocabulary of " +
               std::to_string(n_vocab) + " tokens";
      }
    }
    done += n;
  }

  // The model architecture, which llama.cpp checks by throwing
  uint32_t arch_len = 0;
  f.read(reinterpret_cast<char *>(&arch_len), sizeof(arch_len));
  if (!f) return "cannot read the model architecture of the session";
  if (arch_len != arch.size()) {
    return "the model architecture of the session has length " +
           std::to_string(arch_len) + ", loaded: '" + arch + "'";
  }
  std::string file_arch(arch_len, '\0');
  f.read(file_arch.data(), arch_len);
  if (!f || file_arch != arch) {
    return "the session was written for model architecture '" + file_arch +
           "', loaded: '" + arch + "'";
  }
  return "";
}

// The architecture name of the loaded model, as a session file stores it.
static std::string loaded_model_arch() {
  if (g_model == nullptr || *g_model == nullptr) return "";
  char buf[128];
  buf[0] = '\0';
  llama_model_meta_val_str(*g_model, "general.architecture", buf, sizeof(buf));
  return std::string(buf);
}

void stamp_uploaded_prompt_cache() {
  IC_API ic_api(CanisterUpdate{std::string(__func__)}, false);
  if (!has_admin_update_role(ic_api)) {
    send_access_denied_api_error(ic_api);
    return;
  }

  CandidTypePrincipal caller = ic_api.get_caller();
  std::string principal_id = caller.get_text();

  std::string promptcache{""};
  std::string stamp{""};
  std::string filesha256{""};
  CandidTypeRecord r_in;
  r_in.append("promptcache", CandidTypeText{&promptcache});
  r_in.append("stamp", CandidTypeText{&stamp});
  r_in.append("filesha256", CandidTypeText{&filesha256});
  ic_api.from_wire(r_in);

  const std::string func = __func__;
  auto send_error = [&](const std::string &msg) {
    ic_api.to_wire(CandidTypeVariant{
        "Err", CandidTypeVariant{"Other", CandidTypeText{func + ": " + msg}}});
  };

  if (session_busy(utf8_session_key(promptcache, principal_id), __func__)) {
    send_error(SESSION_BUSY_ERROR);
    return;
  }

  // Each principal has their own cache folder
  std::string filename;
  std::string error_msg;
  if (!get_canister_path_session(promptcache, principal_id, filename,
                                 error_msg)) {
    send_error(error_msg);
    return;
  }
  if (filename.empty()) {
    send_error("promptcache is empty");
    return;
  }
  if (is_prompt_cache_sidecar(filename)) {
    send_error(sidecar_name_error(promptcache));
    return;
  }

  // (a) A model and context to compare against
  llama_context *ctx = icpp_persisted_ctx();
  const std::string current[3] = {
      prompt_cache_format(), prompt_cache_model_id(), prompt_cache_layout_id()};
  const std::string arch = loaded_model_arch();
  if (ctx == nullptr || current[1].empty() || current[2].empty() ||
      arch.empty()) {
    send_error("no model is loaded; load the model the cache was written "
               "with first");
    return;
  }

  // (b) The file was uploaded completely and not changed since
  auto metadata = get_live_file_metadata(filename);
  if (!metadata.has_value()) {
    send_error("no completed upload of " + filename +
               " (upload it with upload_prompt_cache_chunk first)");
    return;
  }

  // (c) The bytes arrived intact
  if (filesha256 != metadata->sha256) {
    send_error("filesha256 " + filesha256 + " does not match the uploaded " +
               metadata->sha256);
    return;
  }

  // (d) The stamp the cache was downloaded with matches this build, the
  // loaded model and the loaded context layout
  std::istringstream stamp_in(stamp);
  static const char *const LINE_NAMES[3] = {"format", "model",
                                            "context layout"};
  for (int i = 0; i < 3; ++i) {
    std::string line;
    std::getline(stamp_in, line);
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (line != current[i]) {
      send_error(std::string("the ") + LINE_NAMES[i] +
                 " does not match. stamp: '" + line + "'; loaded: '" +
                 current[i] + "'");
      return;
    }
  }

  // (e) The head of the session file fits the loaded model and context
  const std::string header_error =
      prompt_cache_header_error(filename, ctx, arch);
  if (!header_error.empty()) {
    send_error(header_error);
    return;
  }

  // The canister writes its own stamp, never the client's text. There are no
  // logits for these bytes: the first run re-decodes the last token.
  prompt_cache_write_format_stamp(filename);
  prompt_cache_remove_logits(filename);
  if (!prompt_cache_format_is_current(filename)) {
    prompt_cache_remove_stamp(filename);
    send_error("could not write the stamp of " + filename);
    return;
  }

  CandidTypeRecord status_code_record;
  status_code_record.append("status_code", CandidTypeNat16{200});
  ic_api.to_wire(CandidTypeVariant{"Ok", status_code_record});
}
