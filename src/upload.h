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

// Remove a metadata record by filename. Returns true if a matching record
// was found and removed from the in-memory vector, false otherwise. The
// underlying save_file_metadata() is best-effort; on save failure this still
// returns true (the record IS removed in memory). Callers that need a
// stronger durability signal should swap to a richer return type.
bool delete_file_metadata(const std::string &filename);
