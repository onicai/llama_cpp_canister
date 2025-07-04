#pragma once

#include "wasm_symbol.h"

#include "ic_api.h"
#include <cstdint>
#include <string>
#include <vector>

void filesystem_remove()
    WASM_SYMBOL_EXPORTED("canister_update filesystem_remove");

bool filesystem_remove_(IC_API &ic_api, const std::string &filename);
std::uint64_t filesystem_file_size_(const std::string &filename);