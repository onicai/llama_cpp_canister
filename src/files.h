#pragma once

#include "wasm_symbol.h"

#include "ic_api.h"
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

void filesystem_remove()
    WASM_SYMBOL_EXPORTED("canister_update filesystem_remove");
void filesystem_file_size()
    WASM_SYMBOL_EXPORTED("canister_query filesystem_file_size");
void get_creation_timestamp_ns()
    WASM_SYMBOL_EXPORTED("canister_query get_creation_timestamp_ns");
void recursive_dir_content_query()
    WASM_SYMBOL_EXPORTED("canister_query recursive_dir_content_query");
void recursive_dir_content_update()
    WASM_SYMBOL_EXPORTED("canister_update recursive_dir_content_update");

void recursive_dir_content_(IC_API &ic_api);
bool filesystem_remove_(IC_API &ic_api, const std::string &filename, bool all,
                        bool to_wire = true);
std::uint64_t filesystem_file_size_(IC_API &ic_api, const std::string &filename,
                                    bool to_wire = true);
std::uint64_t get_creation_timestamp_ns_(IC_API &ic_api,
                                         const std::string &filename,
                                         bool to_wire = true);

struct FileEntry {
  std::string filename;
  std::string filetype; // "file" or "directory"
};
std::vector<FileEntry>
list_directory_contents(const std::filesystem::path &dir,
                        const std::uint64_t &max_entries);

// Helper function to retrieve the last write time of a file
// NOTE: on the IC, this is actually the creation time, not the last write time
std::filesystem::file_time_type
get_last_write_time(const std::filesystem::path &file, std::error_code &ec);
