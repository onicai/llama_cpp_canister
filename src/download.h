#pragma once

#include "wasm_symbol.h"

#include "ic_api.h"
#include <cstdint>
#include <string>

void file_download_chunk()
    WASM_SYMBOL_EXPORTED("canister_query file_download_chunk");

void file_download_chunk_(IC_API &ic_api, const std::string &filename,
                          const uint64_t &chunksize, const uint64_t &offset);