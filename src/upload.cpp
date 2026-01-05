#include "upload.h"
#include "auth.h"
#include "http.h"
#include "ready.h"
#include "utils.h"

// This library is included with icpp-pro
#include "hash-library/sha256.h"

#include <cstdint>
#include <fstream>
#include <iostream>
#include <optional>
#include <stdio.h>
#include <string>

#include "ic_api.h"

// Ephemeral state for an in‑flight upload
// Used to make it idempotent by checking if offset is equal to previous_offset,
// and if so, just return without doing anything
// This is important to properly handle timeouts...
struct UploadSession {
  bool is_first_chunk;
  uint64_t previous_offset;
  SHA256 sha256_state; // Calculate SHA256 hash for the file we're uploading
      // It is callers responsibility to upload one file from start to finish
};
// One upload session per filename
static std::unordered_map<std::string, UploadSession> upload_sessions;

// Maintain a list of files that are uploaded to the canister with metadata
// (filename, filesize, sha256) to be used for validation
struct FileMetadata {
  std::string filename;
  uint64_t filesize;
  std::string sha256;
};

// In-memory cache of file metadata
static std::vector<FileMetadata> uploaded_files;

// File path to store metadata persistently
const std::string METADATA_FILE = "uploaded_files_metadata.dat";

// Save the metadata to disk
void save_file_metadata() {
  std::ofstream file(METADATA_FILE, std::ios::binary | std::ios::trunc);
  if (!file.is_open()) {
    std::cerr << "Failed to open metadata file for writing" << std::endl;
    return;
  }

  // Write number of entries
  uint64_t count = uploaded_files.size();
  file.write(reinterpret_cast<const char *>(&count), sizeof(count));

  // Write each entry
  for (const auto &metadata : uploaded_files) {
    // Write filename
    uint64_t filename_size = metadata.filename.size();
    file.write(reinterpret_cast<const char *>(&filename_size),
               sizeof(filename_size));
    file.write(metadata.filename.c_str(), filename_size);

    // Write filesize
    file.write(reinterpret_cast<const char *>(&metadata.filesize),
               sizeof(metadata.filesize));

    // Write SHA256 hash
    uint64_t sha256_size = metadata.sha256.size();
    file.write(reinterpret_cast<const char *>(&sha256_size),
               sizeof(sha256_size));
    file.write(metadata.sha256.c_str(), sha256_size);
  }

  file.close();
}

// Load the metadata from disk
void load_file_metadata() {
  std::ifstream file(METADATA_FILE, std::ios::binary);
  if (!file.is_open()) {
    std::cout << "Metadata file not found, starting with empty list"
              << std::endl;
    return;
  }

  uploaded_files.clear();

  // Read number of entries
  uint64_t count = 0;
  file.read(reinterpret_cast<char *>(&count), sizeof(count));

  // Read each entry
  for (uint64_t i = 0; i < count; i++) {
    FileMetadata metadata = {};

    // Read filename
    uint64_t filename_size = 0;
    file.read(reinterpret_cast<char *>(&filename_size), sizeof(filename_size));
    if (filename_size > MAX_FILENAME_SIZE) {
      std::cout << "Corrupted metadata: filename_size " << filename_size
                << " exceeds limit " << MAX_FILENAME_SIZE << std::endl;
      break;
    }
    metadata.filename.resize(filename_size);
    file.read(&metadata.filename[0], filename_size);

    // Read filesize
    file.read(reinterpret_cast<char *>(&metadata.filesize),
              sizeof(metadata.filesize));

    // Read SHA256 hash
    uint64_t sha256_size = 0;
    file.read(reinterpret_cast<char *>(&sha256_size), sizeof(sha256_size));
    if (sha256_size > MAX_SHA256_SIZE) {
      std::cout << "Corrupted metadata: sha256_size " << sha256_size
                << " exceeds limit " << MAX_SHA256_SIZE << std::endl;
      break;
    }
    metadata.sha256.resize(sha256_size);
    file.read(&metadata.sha256[0], sha256_size);

    uploaded_files.push_back(metadata);
  }

  file.close();
}

// Add or update file metadata
void update_file_metadata(const std::string &filename, uint64_t filesize,
                          const std::string &sha256) {
  // Always load the metadata from disk first
  load_file_metadata();

  // Check if file already exists in our list
  for (auto &metadata : uploaded_files) {
    if (metadata.filename == filename) {
      // Update existing entry
      metadata.filesize = filesize;
      metadata.sha256 = sha256;
      save_file_metadata();
      return;
    }
  }

  // Add new entry
  uploaded_files.push_back({filename, filesize, sha256});
  save_file_metadata();
}

// Get file metadata by filename
std::optional<FileMetadata> get_file_metadata(const std::string &filename) {
  // Always load the metadata from disk first
  load_file_metadata();

  for (const auto &metadata : uploaded_files) {
    if (metadata.filename == filename) {
      return metadata;  // Return by value, not pointer
    }
  }
  return std::nullopt;
}

// Delete file metadata by filename
void delete_file_metadata(const std::string &filename) {
  // Always load the metadata from disk first
  load_file_metadata();

  for (auto it = uploaded_files.begin(); it != uploaded_files.end(); ++it) {
    if (it->filename == filename) {
      uploaded_files.erase(it);
      save_file_metadata();
      return;
    }
  }
}

void print_file_upload_summary(const std::string &filename,
                               const std::vector<uint8_t> &v,
                               const std::uint64_t &offset,
                               const std::uint64_t &filesize,
                               const std::string &filesha256) {
  std::string msg;
  msg += filename + ": filesize=" + std::to_string(filesize) +
         "; filesha256=" + filesha256;
  std::cout << "llama_cpp: " << std::string(__func__) << " - " << msg
            << std::endl;
}

void file_upload_chunk() {
  IC_API ic_api(CanisterUpdate{std::string(__func__)}, false);
  if (!has_admin_update_role(ic_api)) {
    send_access_denied_api_error(ic_api);
    return;
  }

  // Get filename and the chunk to write to it
  std::string filename{""};
  std::vector<uint8_t> v;
  uint64_t chunksize{0};
  uint64_t offset{0};

  CandidTypeRecord r_in;
  r_in.append("filename", CandidTypeText{&filename});
  r_in.append("chunk", CandidTypeVecNat8{&v});
  r_in.append("chunksize", CandidTypeNat64{&chunksize});
  r_in.append("offset", CandidTypeNat64{&offset});
  ic_api.from_wire(r_in);

  file_upload_chunk_(ic_api, filename, v, chunksize, offset);
}

void file_upload_chunk_(IC_API &ic_api, const std::string &filename,
                        const std::vector<uint8_t> &v,
                        const uint64_t &chunksize, const uint64_t &offset) {

  // Validate chunksize before processing
  if (chunksize > MAX_CHUNK_SIZE) {
    ic_api.to_wire(CandidTypeVariant{
        "Err", CandidTypeVariant{
                   "Other", CandidTypeText{
                                std::string(__func__) + ": chunksize " +
                                std::to_string(chunksize) + " exceeds limit " +
                                std::to_string(MAX_CHUNK_SIZE)}}});
    return;
  }

  // Make sure there's a session entry for this filename
  auto &session = upload_sessions[filename];
  if (offset == 0) {
    // First chunk for this file: (re)initialize
    session.is_first_chunk = true;
    session.previous_offset = 0;
    session.sha256_state = SHA256();
  }

  // Open an ofstream
  std::ofstream of_stream;
  std::string msg;
  std::ios_base::openmode mode = std::ios::binary;
  if (offset == 0) {
    mode |= std::ios::trunc;         // truncate the file to zero length
    session.is_first_chunk = true;   // reset the first chunk flag
    session.previous_offset = 0;     // reset the previous offset
    session.sha256_state = SHA256(); // reset the SHA256 state
  }
  if (!open_ofstream(filename, mode, of_stream, msg)) {
    ic_api.to_wire(CandidTypeVariant{
        "Err", CandidTypeVariant{"Other", CandidTypeText{std::string(__func__) +
                                                         ": " + msg}}});
    return;
  }

  // Check if we already handled this chunk
  if (!session.is_first_chunk && offset == session.previous_offset) {
    std::string msg = "Already handled this chunk";
    std::cout << "llama_cpp: " << std::string(__func__) << " - " << msg
              << std::endl;
    // This is OK, just send back the current status!
    // Get the metadata for the file
    auto metadata = get_file_metadata(filename);
    if (!metadata.has_value()) {
      std::string msg =
          "Already handled this chunk, but Metadata for this file are not found: " +
          filename;
      ic_api.to_wire(CandidTypeVariant{
          "Err",
          CandidTypeVariant{
              "Other", CandidTypeText{std::string(__func__) + ": " + msg}}});
      return;
    }
    uint64_t filesize = metadata->filesize;
    std::string filesha256 = metadata->sha256;
    CandidTypeRecord file_upload_record;
    file_upload_record.append("filename", CandidTypeText{filename});
    file_upload_record.append("filesize", CandidTypeNat64{filesize});
    file_upload_record.append("filesha256", CandidTypeText{filesha256});
    ic_api.to_wire(
        CandidTypeVariant{"Ok", CandidTypeRecord{file_upload_record}});
    return;
  }
  session.is_first_chunk = false;
  session.previous_offset = offset;

  // Write 'v' to 'filename', starting at 'offset'
  of_stream.seekp(offset);
  of_stream.write(reinterpret_cast<const char *>(v.data()), v.size());

  // Check for integer overflow before calculating filesize
  if (offset > UINT64_MAX - v.size()) {
    ic_api.to_wire(CandidTypeVariant{
        "Err",
        CandidTypeVariant{
            "Other",
            CandidTypeText{std::string(__func__) +
                           ": Integer overflow in filesize calculation"}}});
    return;
  }

  // Update the file metadata, including the SHA256 hash using the streaming interface
  uint64_t filesize = offset + v.size();

  // Add this chunk to the hash calculation
  session.sha256_state.add(reinterpret_cast<const char *>(v.data()), v.size());

  // Get the current hash (we'll update it again if more chunks come)
  std::string filesha256 = session.sha256_state.getHash();

  update_file_metadata(filename, filesize, filesha256);

  print_file_upload_summary(filename, v, offset, filesize, filesha256);

  // Return the status over the wire
  CandidTypeRecord file_upload_record;
  file_upload_record.append("filename", CandidTypeText{filename});
  file_upload_record.append("filesize", CandidTypeNat64{filesize});
  file_upload_record.append("filesha256", CandidTypeText{filesha256});
  ic_api.to_wire(CandidTypeVariant{"Ok", CandidTypeRecord{file_upload_record}});
}

void uploaded_file_details() {
  // Returns the metadata for an uploaded file

  IC_API ic_api(CanisterQuery{std::string(__func__)}, false);
  if (!has_admin_query_role(ic_api)) {
    send_access_denied_api_error(ic_api);
    return;
  }

  // Get filename
  std::string filename{""};

  CandidTypeRecord r_in;
  r_in.append("filename", CandidTypeText{&filename});
  ic_api.from_wire(r_in);

  uploaded_file_details_(ic_api, filename);
}

void uploaded_file_details_(IC_API &ic_api, const std::string &filename) {
  // Returns the metadata for an uploaded file
  std::cout << "llama_cpp:" << std::string(__func__) << "Filename: " << filename
            << std::endl;

  // Get the metadata for the file
  auto metadata = get_file_metadata(filename);
  if (!metadata.has_value()) {
    std::string msg = "Metadata for this file are not found: " + filename;
    ic_api.to_wire(CandidTypeVariant{
        "Err", CandidTypeVariant{"Other", CandidTypeText{std::string(__func__) +
                                                         ": " + msg}}});
    return;
  }
  uint64_t filesize = metadata->filesize;
  std::string filesha256 = metadata->sha256;

  // Return the file details over the wire
  CandidTypeRecord file_upload_record;
  file_upload_record.append("filename", CandidTypeText{filename});
  file_upload_record.append("filesize", CandidTypeNat64{filesize});
  file_upload_record.append("filesha256", CandidTypeText{filesha256});
  ic_api.to_wire(CandidTypeVariant{"Ok", CandidTypeRecord{file_upload_record}});
}