#include "files.h"
#include "auth.h"
#include "http.h"
#include "ready.h"
#include "utils.h"

// This library is included with icpp-pro
#include "hash-library/sha256.h"

#include <cstdint>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdio.h>
#include <string>
#include <vector>

#include "ic_api.h"

void filesystem_file_size() {
  IC_API ic_api(CanisterQuery{std::string(__func__)}, false);
  if (!is_caller_a_controller(ic_api)) return;

  // Get filename
  std::string filename{""};

  CandidTypeRecord r_in;
  r_in.append("filename", CandidTypeText{&filename});
  ic_api.from_wire(r_in);

  filesystem_file_size_(ic_api, filename, true);
}

void filesystem_remove() {
  IC_API ic_api(CanisterUpdate{std::string(__func__)}, false);
  if (!is_caller_a_controller(ic_api)) return;

  // Get filename
  std::string filename{""};

  CandidTypeRecord r_in;
  r_in.append("filename", CandidTypeText{&filename});
  ic_api.from_wire(r_in);

  filesystem_remove_(ic_api, filename, true);
}

void recursive_dir_content() {
  IC_API ic_api(CanisterQuery{std::string(__func__)}, false);
  if (!is_caller_a_controller(ic_api)) return;

  // Get directory name
  std::string dir{""};

  CandidTypeRecord r_in;
  r_in.append("dir", CandidTypeText{&dir});
  ic_api.from_wire(r_in);

  // Check if the directory exists
  std::error_code ec;
  if (!std::filesystem::exists(dir, ec) ||
      !std::filesystem::is_directory(dir, ec)) {
    std::string msg = "Directory does not exist: " + dir + "\n";
    std::cout << "llama_cpp: " << std::string(__func__) << " - " << msg
              << std::endl;
    ic_api.to_wire(CandidTypeVariant{
        "Err", CandidTypeVariant{"Other", CandidTypeText{std::string(__func__) +
                                                         ": " + msg}}});
    return;
  }

  // List the contents of the directory
  std::vector<FileEntry> contents =
      list_directory_contents(std::filesystem::path(dir));

  // reformat it to proper output format for Candid interface
  std::vector<std::string> filenames;
  std::vector<std::string> filetypes;
  std::vector<std::uint64_t> filesizes;
  for (const auto &entry : contents) {
    filenames.push_back(entry.filename);
    filetypes.push_back(entry.filetype);
    filesizes.push_back(entry.filesize);
  }
  CandidTypeRecord r_file_entries;
  r_file_entries.append("filename", CandidTypeVecText{filenames});
  r_file_entries.append("filetype", CandidTypeVecText{filetypes});
  r_file_entries.append("filesize", CandidTypeVecNat64{filesizes});
  ic_api.to_wire(CandidTypeVariant{"Ok", CandidTypeVecRecord{r_file_entries}});
}

bool filesystem_remove_(IC_API &ic_api, const std::string &filename,
                        bool to_wire) {
  // Use the non-throwing version of std::filesystem::remove
  std::error_code ec;
  std::string msg;
  bool error = false;
  bool removed = false;
  std::uint64_t filesize = 0;

  bool exists = std::filesystem::exists(filename, ec);
  if (ec) {
    error = true;
    msg = "Error: " + ec.message() + "\n";
  } else if (!exists) {
    msg = "File does not exist: " + filename + "\n";
  } else {
    filesize = filesystem_file_size_(ic_api, filename, false);
    removed = std::filesystem::remove(filename, ec);
    if (ec) {
      error = true;
      msg += "Failed to remove file: " + filename + ": " + ec.message();
    } else if (!removed) {
      msg += "File did not exist: " + filename;
    } else {
      msg += "File removed successfully: " + filename;
    }
  }

  std::cout << "llama_cpp: " << std::string(__func__) << " - " << msg
            << std::endl;

  if (to_wire) {
    if (error) {
      ic_api.to_wire(CandidTypeVariant{
          "Err",
          CandidTypeVariant{
              "Other", CandidTypeText{std::string(__func__) + ": " + msg}}});
    } else {
      // Return the status over the wire (caller must immediately return from endpoint)
      CandidTypeRecord filesystem_remove_record;
      filesystem_remove_record.append("exists", CandidTypeBool{exists});
      filesystem_remove_record.append("removed", CandidTypeBool{removed});
      filesystem_remove_record.append("filename", CandidTypeText{filename});
      filesystem_remove_record.append("filesize", CandidTypeNat64{filesize});
      filesystem_remove_record.append("msg", CandidTypeText{msg});
      ic_api.to_wire(
          CandidTypeVariant{"Ok", CandidTypeRecord{filesystem_remove_record}});
    }
  }
  return removed;
}

std::uint64_t filesystem_file_size_(IC_API &ic_api, const std::string &filename,
                                    bool to_wire) {
  std::error_code ec;
  std::string msg;
  bool error = false;
  std::uint64_t filesize = 0;

  bool exists = std::filesystem::exists(filename, ec);
  if (ec) {
    error = true;
    msg = "Error: " + ec.message() + "\n";
  } else if (!exists) {
    msg = "File does not exist: " + filename + "\n";
  } else {
    msg = "File exists: " + filename + "\n";
    // Use the non-throwing version of std::filesystem::file_size
    auto size = std::filesystem::file_size(filename, ec);
    if (ec) {
      error = true;
      msg += "Error: " + ec.message() + "\n";
    } else {
      msg += "File size: " + std::to_string(size) + " bytes" + "\n";
      filesize = static_cast<std::uint64_t>(size);
    }
  }

  std::cout << "llama_cpp: " << std::string(__func__) << " - " << msg
            << std::endl;

  if (to_wire) {
    // Return the filesize over the wire (caller must immediately return from endpoint)
    CandidTypeRecord filesystem_file_size_record;
    filesystem_file_size_record.append("exists", CandidTypeBool{exists});
    filesystem_file_size_record.append("filename", CandidTypeText{filename});
    filesystem_file_size_record.append("filesize", CandidTypeNat64{filesize});
    filesystem_file_size_record.append("msg", CandidTypeText{msg});
    ic_api.to_wire(
        CandidTypeVariant{"Ok", CandidTypeRecord{filesystem_file_size_record}});
  }
  return filesize;
}

std::vector<FileEntry>
list_directory_contents(const std::filesystem::path &dir) {
  std::vector<FileEntry> entries;
  std::error_code ec;

  if (!std::filesystem::exists(dir, ec) ||
      !std::filesystem::is_directory(dir, ec)) {
    return entries;
  }

  for (const auto &entry :
       std::filesystem::recursive_directory_iterator(dir, ec)) {
    if (ec) continue;

    FileEntry fe;
    fe.filename = entry.path().string();

    if (entry.is_directory(ec)) {
      fe.filetype = "directory";
      fe.filesize = 0;
    } else if (entry.is_regular_file(ec)) {
      fe.filetype = "file";

      auto size = std::filesystem::file_size(entry.path(), ec);
      if (ec || size > std::numeric_limits<std::uint64_t>::max()) {
        fe.filesize = 0;
      } else {
        fe.filesize = static_cast<std::uint64_t>(size);
      }
    } else {
      fe.filetype = "other";
      fe.filesize = 0;
    }

    entries.push_back(fe);
  }

  return entries;
}
