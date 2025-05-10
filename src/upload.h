#pragma once

#include "wasm_symbol.h"

#include "ic_api.h"
#include <cstdint>
#include <string>
#include <vector>

void file_upload_chunk()
    WASM_SYMBOL_EXPORTED("canister_update file_upload_chunk");
void uploaded_file_details()
    WASM_SYMBOL_EXPORTED("canister_query uploaded_file_details");

void file_upload_chunk_(IC_API &ic_api, const std::string &filename,
                        const std::vector<uint8_t> &v,
                        const uint64_t &chunksize, const uint64_t &offset);
void uploaded_file_details_(IC_API &ic_api, const std::string &filename);
