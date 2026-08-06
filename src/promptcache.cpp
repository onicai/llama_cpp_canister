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

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <system_error>

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

// Bump this whenever a llama.cpp upgrade changes the session serialization.
//   1 = llama.cpp b4531 (6152129d) and earlier -- never actually stamped
//   2 = llama.cpp b10076 (305ba519), llama_memory_* refactor
static const char *PROMPT_CACHE_FORMAT = "llama_cpp_canister-prompt-cache-v2";

static std::string
prompt_cache_stamp_path(const std::string &canister_path_session) {
  return canister_path_session + ".icppfmt";
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

bool prompt_cache_format_is_current(const std::string &canister_path_session) {
  const std::string stamp_path = prompt_cache_stamp_path(canister_path_session);

  std::ifstream f(stamp_path);
  if (!f.is_open()) return false; // unstamped => written by an older build

  std::string stamp;
  std::getline(f, stamp);
  if (stamp != PROMPT_CACHE_FORMAT) return false;

  // Second line: the model the cache was written with. A cache is only valid
  // for the model that produced it -- see prompt_cache_discard_if_stale.
  // Absent (older stamp) counts as a mismatch, so those caches are discarded.
  std::string stamped_model;
  if (!std::getline(f, stamped_model)) return false;

  const std::string current_model = prompt_cache_model_id();
  // No model loaded => nothing to compare against; do not discard on that basis.
  if (current_model.empty()) return true;

  return stamped_model == current_model;
}

void prompt_cache_remove_stamp(const std::string &canister_path_session) {
  // Drop the sidecar so the cache counts as UNSTAMPED and will be discarded.
  // Must be called by anything that removes or overwrites the cache bytes:
  // a stamp that outlives the file it describes is worse than no stamp at all,
  // because prompt_cache_discard_if_stale() would then vouch for content this
  // build never wrote and llama.cpp would trap on it.
  if (canister_path_session.empty()) return;
  std::error_code ec;
  std::filesystem::remove(prompt_cache_stamp_path(canister_path_session), ec);
}

void prompt_cache_copy_stamp(const std::string &from_session,
                             const std::string &to_session) {
  // The stamp describes the BYTES, so it must travel with them. A cache copied
  // from another of this caller's caches really was written by this build with
  // this model, so its stamp stays valid and the restored cache stays warm.
  // (Without this, save/restore via copy_prompt_cache silently degrades into a
  // cold start on every restore.)
  const std::string from_stamp = prompt_cache_stamp_path(from_session);
  const std::string to_stamp = prompt_cache_stamp_path(to_session);

  std::error_code ec;
  std::filesystem::remove(to_stamp, ec);
  if (std::filesystem::exists(from_stamp)) {
    std::filesystem::copy(from_stamp, to_stamp, ec);
  }
}

void prompt_cache_write_format_stamp(const std::string &canister_path_session) {
  std::ofstream f(prompt_cache_stamp_path(canister_path_session),
                  std::ios::trunc);
  if (f.is_open()) {
    f << PROMPT_CACHE_FORMAT << std::endl;
    f << prompt_cache_model_id() << std::endl;
  }
}

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
  std::filesystem::remove(prompt_cache_stamp_path(canister_path_session), ec);

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

  std::string msg;
  if (!path_session.empty()) {
    // Remove the file if it exists
    if (std::filesystem::exists(path_session)) {
      bool success = std::filesystem::remove(path_session);
      // Never leave the stamp behind: it would vouch for whatever bytes appear
      // at this path next (e.g. an uploaded cache from another build/model).
      prompt_cache_remove_stamp(path_session);
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

  // Each principal has their own cache folder
  std::string filename;
  std::string error_msg;
  if (!get_canister_path_session(promptcache, principal_id, filename,
                                 error_msg)) {
    ic_api.to_wire(CandidTypeVariant{
        "Err", CandidTypeVariant{"Other", CandidTypeText{error_msg}}});
    return;
  }

  // The uploaded bytes did not come from this canister, so any existing stamp
  // no longer describes this file. Drop it: the cache is then treated as
  // unstamped and discarded on first use (a cold start), instead of being
  // handed to llama.cpp, which traps on a session it cannot parse.
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