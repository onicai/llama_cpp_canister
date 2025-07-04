#pragma once

#include "wasm_symbol.h"

#include "ic_api.h"
#include <cstdint>
#include <string>
#include <vector>

void filesystem_remove()
    WASM_SYMBOL_EXPORTED("canister_update filesystem_remove");
void filesystem_file_size()
    WASM_SYMBOL_EXPORTED("canister_query filesystem_file_size");

bool filesystem_remove_(IC_API &ic_api, const std::string &filename,
                        bool to_wire = true);
std::uint64_t filesystem_file_size_(IC_API &ic_api, const std::string &filename,
                                    bool to_wire = true);