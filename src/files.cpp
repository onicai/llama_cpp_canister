#include "files.h"
#include "auth.h"
#include "http.h"
#include "ready.h"
#include "utils.h"

// This library is included with icpp-pro
#include "hash-library/sha256.h"

#include <chrono>
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

void get_creation_timestamp_ns() {
  IC_API ic_api(CanisterQuery{std::string(__func__)}, false);
  if (!is_caller_a_controller(ic_api)) return;

  // Get filename
  std::string filename{""};

  CandidTypeRecord r_in;
  r_in.append("filename", CandidTypeText{&filename});
  ic_api.from_wire(r_in);

  get_creation_timestamp_ns_(ic_api, filename, true);
}

void filesystem_remove() {
  IC_API ic_api(CanisterUpdate{std::string(__func__)}, false);
  if (!is_caller_a_controller(ic_api)) return;

  // Get filename
  std::string filename{""};

  CandidTypeRecord r_in;
  r_in.append("filename", CandidTypeText{&filename});
  ic_api.from_wire(r_in);

  bool all{false};    // remove a single file or empty directory
  bool to_wire{true}; // Return the status over the wire
  filesystem_remove_(ic_api, filename, all, to_wire);
}

void recursive_dir_content_update() {
  IC_API ic_api(CanisterUpdate{std::string(__func__)}, false);
  if (!is_caller_a_controller(ic_api)) return;
  recursive_dir_content_(ic_api);
}

void recursive_dir_content_query() {
  IC_API ic_api(CanisterQuery{std::string(__func__)}, false);
  if (!is_caller_a_controller(ic_api)) return;
  recursive_dir_content_(ic_api);
}

void recursive_dir_content_(IC_API &ic_api) {
  // Get directory name
  std::string dir{""};
  std::uint64_t max_entries{0}; // 0 = no limit

  CandidTypeRecord r_in;
  r_in.append("dir", CandidTypeText{&dir});
  r_in.append("max_entries", CandidTypeNat64{&max_entries});
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
      list_directory_contents(std::filesystem::path(dir), max_entries);

  // reformat it to proper output format for Candid interface
  std::vector<std::string> filenames;
  std::vector<std::string> filetypes;
  for (const auto &entry : contents) {
    filenames.push_back(entry.filename);
    filetypes.push_back(entry.filetype);
  }
  CandidTypeRecord r_file_entries;
  r_file_entries.append("filename", CandidTypeVecText{filenames});
  r_file_entries.append("filetype", CandidTypeVecText{filetypes});
  ic_api.to_wire(CandidTypeVariant{"Ok", CandidTypeVecRecord{r_file_entries}});
}

bool filesystem_remove_(IC_API &ic_api, const std::string &filename, bool all,
                        bool to_wire) {
  // Use the non-throwing version of std::filesystem::remove
  std::error_code ec;
  std::string msg;
  bool error = false;
  bool removed = false;

  // TODO: remove_all is not working in a canister. It works natively, but not in a canister.
  bool exists = std::filesystem::exists(filename, ec);
  if (ec) {
    error = true;
    msg = "Error: " + ec.message() + "\n";
  } else if (!exists) {
    msg = "Path does not exist: " + filename + "\n";
  } else {
    if (all) {
      msg =
          "The std::filesystem::remove_all function is not working in a canister. "
          "You must first empty out all contents of directory: " +
          filename;
      error = true;
      // // Use std::filesystem::remove_all to remove directories and their contents
      // std::cout << "llama_cpp: " << std::string(__func__)
      //           << " - Removing all contents of directory: " << filename
      //           << std::endl;
      // removed = std::filesystem::remove_all(filename, ec);
    } else {
      // Use std::filesystem::remove to remove a single file or empty directory
      removed = std::filesystem::remove(filename, ec);
    }
    if (ec) {
      error = true;
      msg += "Failed to remove: " + filename + ": " + ec.message();
    } else if (!removed) {
      msg += "Path did not exist: " + filename;
    } else {
      msg += "Path removed successfully: " + filename;
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
    error = true;
    msg = "File does not exist: " + filename;
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
    if (error) {
      ic_api.to_wire(CandidTypeVariant{
          "Err", CandidTypeVariant{"Other", CandidTypeText{msg}}});
    } else {
      // Return the filesize over the wire (caller must immediately return from endpoint)
      CandidTypeRecord filesystem_file_size_record;
      filesystem_file_size_record.append("exists", CandidTypeBool{exists});
      filesystem_file_size_record.append("filename", CandidTypeText{filename});
      filesystem_file_size_record.append("filesize", CandidTypeNat64{filesize});
      filesystem_file_size_record.append("msg", CandidTypeText{msg});
      ic_api.to_wire(CandidTypeVariant{
          "Ok", CandidTypeRecord{filesystem_file_size_record}});
    }
  }
  return filesize;
}

std::uint64_t get_creation_timestamp_ns_(IC_API &ic_api,
                                         const std::string &filename,
                                         bool to_wire) {
  std::error_code ec;
  std::string msg;
  bool error = false;
  std::uint64_t timestamp_ns = 0;
  std::uint64_t age_seconds = 0;

  bool exists = std::filesystem::exists(filename, ec);
  if (ec) {
    error = true;
    msg = "Error: " + ec.message() + "\n";
  } else if (!exists) {
    error = true;
    msg = "File does not exist: " + filename;
  } else {
    msg = "File exists: " + filename + "\n";
    // Use the non-throwing version of std::filesystem::last_write_time
    // NOTE: on the IC, this is actually the creation time, not the last write time
    auto ftime = std::filesystem::last_write_time(filename, ec);
    if (ec) {
      error = true;
      msg += "Error: " + ec.message() + "\n";
    } else {
      // Get time since epoch in nanoseconds
      auto nanos_since_epoch =
          std::chrono::duration_cast<std::chrono::nanoseconds>(
              ftime.time_since_epoch())
              .count();
      timestamp_ns = static_cast<std::uint64_t>(nanos_since_epoch);

      // Calculate age in seconds
      auto now = std::chrono::system_clock::now();
      auto age = std::chrono::duration_cast<std::chrono::seconds>(
                     now.time_since_epoch() - ftime.time_since_epoch())
                     .count();
      age_seconds = static_cast<std::uint64_t>(age);

      msg +=
          "File creation timestamp_ns: " + std::to_string(nanos_since_epoch) +
          " ns" + "\n" + "File creation age: " + std::to_string(age_seconds) +
          " seconds" + "\n";
    }
  }

  std::cout << "llama_cpp: " << std::string(__func__) << " - " << msg
            << std::endl;

  if (to_wire) {
    if (error) {
      ic_api.to_wire(CandidTypeVariant{
          "Err", CandidTypeVariant{"Other", CandidTypeText{msg}}});
    } else {
      // Return the filesize over the wire (caller must immediately return from endpoint)
      CandidTypeRecord filesystem_timestamp_record;
      filesystem_timestamp_record.append("exists", CandidTypeBool{exists});
      filesystem_timestamp_record.append("filename", CandidTypeText{filename});
      filesystem_timestamp_record.append("timestamp_ns",
                                         CandidTypeNat64{timestamp_ns});
      filesystem_timestamp_record.append("age_seconds",
                                         CandidTypeNat64{age_seconds});
      filesystem_timestamp_record.append("msg", CandidTypeText{msg});
      ic_api.to_wire(CandidTypeVariant{
          "Ok", CandidTypeRecord{filesystem_timestamp_record}});
    }
  }
  return timestamp_ns;
}

std::vector<FileEntry>
list_directory_contents(const std::filesystem::path &dir,
                        const std::uint64_t &max_entries) {
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
    } else if (entry.is_regular_file(ec)) {
      fe.filetype = "file";
    } else {
      fe.filetype = "other";
    }

    entries.push_back(fe);

    // print size of entries every 100 entries
    if (entries.size() % 1000 == 0) {
      std::cout << "llama_cpp: " << std::string(__func__) << " - " << dir
                << ": found " << entries.size() << " entries." << std::endl;
    }

    if (max_entries > 0 && entries.size() >= max_entries) {
      std::cout << "llama_cpp: " << std::string(__func__)
                << " - Warning: Limiting directory listing contents to "
                << max_entries << " entries.\n";
      break;
    }
  }

  std::cout << "llama_cpp: " << std::string(__func__) << " - " << dir
            << ": found " << entries.size() << " entries." << std::endl;

  return entries;
}

// Helper function to retrieve the last write time of a file
// NOTE: on the IC, this is actually the creation time, not the last write time
std::filesystem::file_time_type
get_last_write_time(const std::filesystem::path &file, std::error_code &ec) {
  return std::filesystem::last_write_time(file, ec);
}