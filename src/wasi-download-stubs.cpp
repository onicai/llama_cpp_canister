// WASI stubs for llama.cpp's model-download machinery.
//
// common/download.cpp and common/hf-cache.cpp depend on cpp-httplib, which has
// no WASI backend, so they are excluded from icpp.toml. A canister never
// downloads: models are uploaded into the virtual filesystem via the
// file_upload_chunk endpoint (see src/upload.cpp).
//
// Functions that would perform network or Docker actions trap loudly, so a
// stray code path is diagnosed rather than silently returning wrong data.
// Pure queries return empty results, which is truthful here — a canister has
// no HF cache.
//
// Adapted from instruction-bounded-inference-artifact
// MIT (c) 2026 Julien Aerni, Simeon Fluck, Dustin Becker

#include "common/download.h"
#include "common/hf-cache.h"

#include "ic_api.h"

// --- network / Docker actions: unreachable in a canister ---

std::pair<long, std::vector<char>>
common_remote_get_content(const std::string &url,
                          const common_remote_params &params) {
  IC_API::trap(
      "common_remote_get_content: no network in a canister (url=" + url + ")");
}

void common_download_run_tasks(const std::vector<common_download_task> &tasks) {
  // A local-file load produces an empty task list; that is the normal path in a
  // canister and must not trap. Only a genuine download request (non-empty) is
  // unreachable here.
  if (tasks.empty()) return;
  IC_API::trap("common_download_run_tasks: no network in a canister");
}

std::vector<std::string> common_download_get_all_parts(const std::string &url) {
  IC_API::trap("common_download_get_all_parts: no network in a canister (url=" +
               url + ")");
}

int common_download_file_single(const std::string &url, const std::string &path,
                                const common_download_opts &opts,
                                bool skip_etag) {
  IC_API::trap("common_download_file_single: no network in a canister (url=" +
               url + ")");
}

std::string common_docker_resolve_model(const std::string &docker) {
  IC_API::trap(
      "common_docker_resolve_model: no Docker registry in a canister (" +
      docker + ")");
}

// --- pure queries: a canister has no HF cache ---

std::pair<std::string, std::string>
common_download_split_repo_tag(const std::string &hf_repo_with_tag) {
  const auto pos = hf_repo_with_tag.find(':');
  if (pos == std::string::npos) {
    return {hf_repo_with_tag, ""};
  }
  return {hf_repo_with_tag.substr(0, pos), hf_repo_with_tag.substr(pos + 1)};
}

std::vector<common_cached_model_info> common_list_cached_models() { return {}; }

bool common_download_remove(const std::string &hf_repo_with_tag) {
  return false;
}

common_download_hf_plan
common_download_get_hf_plan(const common_params_model &model,
                            const common_download_opts &opts) {
  return {};
}

namespace hf_cache {

hf_files get_repo_files(const std::string &repo_id, const std::string &token) {
  return {};
}

hf_files get_cached_files(const std::string &repo_id) { return {}; }

std::string finalize_file(const hf_file &file) { return file.local_path; }

bool remove_cached_repo(const std::string &repo_id) { return false; }

} // namespace hf_cache
