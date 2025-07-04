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
void recursive_dir_content()
    WASM_SYMBOL_EXPORTED("canister_query recursive_dir_content");

bool filesystem_remove_(IC_API &ic_api, const std::string &filename,
                        bool to_wire = true);
std::uint64_t filesystem_file_size_(IC_API &ic_api, const std::string &filename,
                                    bool to_wire = true);

struct FileEntry {
  std::string filename;
  std::string filetype;   // "file" or "directory"
  std::uint64_t filesize; // file size in bytes, 0 for directories or errors
};
std::vector<FileEntry>
list_directory_contents(const std::filesystem::path &dir);