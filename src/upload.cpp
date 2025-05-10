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
#include <stdio.h>
#include <string>

#include "ic_api.h"

// Make it idempotent by checking if offset is equal to previous_offset,
// and if so, just return without doing anything
// This is important to properly handle timeouts...
//     ************************************************
// *** This only works for uploading one file at a time ***
// *** TODO: support multiple files at the same time    ***
// **        likely just by storing these in the FileMetadata:
//           - is_first_chunk
//           - previous_offset
//           - sha256_state
//     ************************************************

static bool is_first_chunk = true;
static uint64_t previous_offset = 0;

// Calculate SHA256 hash for the file we're uploading
// It is callers responsibility to upload one file from start to finish
static SHA256 sha256_state;

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
    FileMetadata metadata;

    // Read filename
    uint64_t filename_size = 0;
    file.read(reinterpret_cast<char *>(&filename_size), sizeof(filename_size));
    metadata.filename.resize(filename_size);
    file.read(&metadata.filename[0], filename_size);

    // Read filesize
    file.read(reinterpret_cast<char *>(&metadata.filesize),
              sizeof(metadata.filesize));

    // Read SHA256 hash
    uint64_t sha256_size = 0;
    file.read(reinterpret_cast<char *>(&sha256_size), sizeof(sha256_size));
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
const FileMetadata *get_file_metadata(const std::string &filename) {
  // Always load the metadata from disk first
  load_file_metadata();

  for (const auto &metadata : uploaded_files) {
    if (metadata.filename == filename) {
      return &metadata;
    }
  }
  return nullptr;
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
  if (!is_caller_a_controller(ic_api)) return;

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

  // Open an ofstream
  std::ofstream of_stream;
  std::string msg;
  std::ios_base::openmode mode = std::ios::binary;
  if (offset == 0) {
    mode |= std::ios::trunc; // truncate the file to zero length
    is_first_chunk = true;   // reset the first chunk flag
    previous_offset = 0;     // reset the previous offset
  }
  if (!open_ofstream(filename, mode, of_stream, msg)) {
    ic_api.to_wire(CandidTypeVariant{
        "Err", CandidTypeVariant{"Other", CandidTypeText{std::string(__func__) +
                                                         ": " + msg}}});
    return;
  }

  // Check if we already handled this chunk
  if (!is_first_chunk && offset == previous_offset) {
    std::string msg = "Already handled this chunk";
    std::cout << "llama_cpp: " << std::string(__func__) << " - " << msg
              << std::endl;
    // This is OK, just send back the current status!
    // Get the metadata for the file
    const FileMetadata *metadata = get_file_metadata(filename);
    if (metadata == nullptr) {
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
  is_first_chunk = false;
  previous_offset = offset;

  // Write 'v' to 'filename', starting at 'offset'
  of_stream.seekp(offset);
  of_stream.write(reinterpret_cast<const char *>(v.data()), v.size());

  // Update the file metadata, including the SHA256 hash using the streaming interface
  uint64_t filesize = offset + v.size();

  // If this is the first chunk (offset == 0) reset the hash state
  if (offset == 0) {
    sha256_state = SHA256();
  }

  // Add this chunk to the hash calculation
  sha256_state.add(reinterpret_cast<const char *>(v.data()), v.size());

  // Get the current hash (we'll update it again if more chunks come)
  std::string filesha256 = sha256_state.getHash();

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
  if (!is_caller_a_controller(ic_api)) return;

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
  const FileMetadata *metadata = get_file_metadata(filename);
  if (metadata == nullptr) {
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