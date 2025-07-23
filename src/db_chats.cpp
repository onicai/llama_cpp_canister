#include "db_chats.h"
#include "auth.h"
#include "common.h"
#include "files.h"
#include "http.h"
#include "main_.h"
#include "max_tokens.h"
#include "run.h"
#include "utils.h"

#include <filesystem>
#include <iostream>
#include <string>
#include <system_error>

#include "ic_api.h"

bool DB_CHATS_ACTIVE = false;

bool is_db_chats_active() { return DB_CHATS_ACTIVE; }

bool get_saved_chats_dir(const std::string &principal_id,
                         std::string &saved_chats_dir, std::string &error_msg) {
  if (!DB_CHATS_ACTIVE) {
    error_msg = std::string(__func__) +
                ": do not call this function if DB_CHATS_ACTIVE is false";
    return false;
  }

  saved_chats_dir = ".canister_cache/" + principal_id + "/db_chats";

  // Make sure that the directory exists, else we cannot create the file
  std::filesystem::path dir_path(saved_chats_dir);
  if (!my_create_directory(dir_path, error_msg)) {
    return false;
  }

  return true;
}

bool db_chats_write_conversation(const std::string &file_path,
                                 const std::string &conversation,
                                 const std::string &principal_id,
                                 std::string &error_msg) {
  if (!DB_CHATS_ACTIVE) {
    error_msg = std::string(__func__) +
                ": do not call this function if DB_CHATS_ACTIVE is false";
    return false;
  }
  std::ofstream ofs(file_path);
  if (ofs.is_open()) {
    ofs << principal_id << std::endl;
    if (!ofs) {
      error_msg = std::string(__func__) + ": Error writing header to file - " +
                  file_path;
      return false;
    }

    ofs << conversation;
    if (!ofs) {
      error_msg = std::string(__func__) +
                  ": Error writing conversation to file - " + file_path;
      return false;
    }

  } else {
    error_msg = std::string(__func__) + ": Failed to open file: " + file_path;
    return false;
  }

  ofs.close();
  return true;
}

bool db_chats_new(const std::string &principal_id, std::string &error_msg) {
  if (!DB_CHATS_ACTIVE) {
    error_msg = std::string(__func__) +
                ": do not call this function if DB_CHATS_ACTIVE is false";
    return false;
  }
  // The chats will be stored in ".saved_chats/<principal_id>"
  std::string saved_chats_dir;
  if (!get_saved_chats_dir(principal_id, saved_chats_dir, error_msg)) {
    return false;
  }

  // Get the current time as a timestamp
  std::time_t now_time = std::time(0);

  // Create a timestamp string
  std::stringstream ss;
  ss << std::put_time(std::localtime(&now_time), "%Y-%m-%d_%H-%M-%S");
  std::string timestamp = ss.str();

  // Full file path with directory and timestamp as name
  std::string file_path = saved_chats_dir + "/" + ss.str();

  // Create and open the file and write the header.
  std::string conversation; // empty conversation
  if (!db_chats_write_conversation(file_path, conversation, principal_id,
                                   error_msg)) {
    return false;
  }

  return true;
}

bool db_chats_clean(const std::string &principal_id, std::string &error_msg) {
  if (!DB_CHATS_ACTIVE) {
    error_msg = std::string(__func__) +
                ": do not call this function if DB_CHATS_ACTIVE is false";
    return false;
  }
  // Each principal can only save max_chats
  uint64_t max_chats = 3; // Just hardcode it for now

  std::string saved_chats_dir;
  if (!get_saved_chats_dir(principal_id, saved_chats_dir, error_msg)) {
    return false;
  }

  std::vector<std::filesystem::directory_entry> files;

  // Error code to store any errors during filesystem operations
  std::error_code ec;

  // Iterate through the directory and collect all regular files
  for (const auto &entry :
       std::filesystem::directory_iterator(saved_chats_dir, ec)) {
    if (ec) {
      error_msg =
          std::string(__func__) + ": Error reading directory: " + ec.message();
      return false;
    }

    if (std::filesystem::is_regular_file(entry, ec) && !ec) {
      files.emplace_back(entry);
    }
  }

  // If the number of files is more than 'n', delete the older ones
  if (files.size() > max_chats) {
    // Sort files by their last write time (most recent first)
    std::sort(files.begin(), files.end(),
              [&ec](const std::filesystem::directory_entry &a,
                    const std::filesystem::directory_entry &b) {
                return get_last_write_time(a.path(), ec) >
                       get_last_write_time(b.path(), ec);
              });
    // keep only max_chats
    for (std::size_t i = max_chats; i < files.size(); ++i) {
      std::filesystem::remove(files[i], ec);
      if (ec) {
        error_msg = std::string(__func__) + ": Error deleting file " +
                    files[i].path().string() + ": " + ec.message();
        return false;
      } else {
        std::cout << "llama_cpp: " << std::string(__func__) << " - "
                  << "Deleted: " << files[i].path().string() << '\n';
      }
    }
  }

  return true;
}

bool db_chats_save_conversation(const std::string &conversation,
                                const std::string &principal_id,
                                std::string &error_msg) {
  if (!DB_CHATS_ACTIVE) {
    error_msg = std::string(__func__) +
                ": do not call this function if DB_CHATS_ACTIVE is false";
    return false;
  }
  std::string saved_chats_dir;
  if (!get_saved_chats_dir(principal_id, saved_chats_dir, error_msg)) {
    return false;
  }

  std::vector<std::filesystem::directory_entry> files;
  std::error_code ec;

  // Iterate through the directory and collect all regular files
  for (const auto &entry :
       std::filesystem::directory_iterator(saved_chats_dir, ec)) {
    if (ec) {
      error_msg =
          std::string(__func__) + ": Error reading directory: " + ec.message();
      return false;
    }

    if (std::filesystem::is_regular_file(entry, ec) && !ec) {
      files.emplace_back(entry);
    }
  }

  if (files.empty()) {
    error_msg = std::string(__func__) + ": No files found in directory";
    return false;
  }

  // Find the file with the most recent last write time
  auto latest_file =
      std::max_element(files.begin(), files.end(),
                       [&ec](const std::filesystem::directory_entry &a,
                             const std::filesystem::directory_entry &b) {
                         return get_last_write_time(a.path(), ec) <
                                get_last_write_time(b.path(), ec);
                       });

  if (ec) {
    error_msg = std::string(__func__) +
                ": Error retrieving last write time: " + ec.message();
    return false;
  }

  std::string file_path = latest_file->path().string();
  if (!db_chats_write_conversation(file_path, conversation, principal_id,
                                   error_msg)) {
    return false;
  }
  return true;
}

// Canister API to retrieve saved chats for authenticated caller
void get_chats() {
  IC_API ic_api(CanisterQuery{std::string(__func__)}, false);
  if (!is_caller_whitelisted(ic_api)) return;

  if (!DB_CHATS_ACTIVE) {
    ic_api.to_wire(CandidTypeVariant{
        "Err", CandidTypeVariant{"Other",
                                 CandidTypeText{std::string(__func__) + ": " +
                                                "DB_CHATS_ACTIVE is false"}}});
    return;
  }

  CandidTypePrincipal caller = ic_api.get_caller();

  // Get the principal ID as a string
  std::string principal_id = caller.get_text();

  // Directory where chats are stored for this principal
  std::string saved_chats_dir;
  std::string error_msg;
  if (!get_saved_chats_dir(principal_id, saved_chats_dir, error_msg)) {
    ic_api.to_wire(CandidTypeVariant{
        "Err", CandidTypeVariant{"Other", CandidTypeText{std::string(__func__) +
                                                         ": " + error_msg}}});
    return;
  }

  // Vectors to store the chats read from file
  std::vector<std::string> timestamps;
  std::vector<std::string> chats;
  std::error_code ec;

  // Iterate through the directory and collect all regular files
  for (const auto &entry :
       std::filesystem::directory_iterator(saved_chats_dir, ec)) {
    if (ec) {
      std::string msg = "Error reading directory: " + ec.message();
      ic_api.to_wire(CandidTypeVariant{
          "Err",
          CandidTypeVariant{
              "Other", CandidTypeText{std::string(__func__) + ": " + msg}}});
      return;
    }

    if (std::filesystem::is_regular_file(entry, ec) && !ec) {
      std::ifstream ifs(entry.path());
      if (!ifs.is_open()) {
        std::string msg = "Failed to open file: " + entry.path().string();
        ic_api.to_wire(CandidTypeVariant{
            "Err",
            CandidTypeVariant{
                "Other", CandidTypeText{std::string(__func__) + ": " + msg}}});
        return;
      }

      // Read the principal ID from the file (first line)
      std::string file_principal_id;
      std::getline(ifs, file_principal_id);

      // Verify the file's principal ID matches the caller's principal ID
      if (file_principal_id != principal_id) {
        std::string msg =
            "Principal id not correct in file: " + entry.path().string();
        ic_api.to_wire(CandidTypeVariant{
            "Err",
            CandidTypeVariant{
                "Other", CandidTypeText{std::string(__func__) + ": " + msg}}});
        return;
      }

      // Read the rest of the file as the chat
      std::string chat((std::istreambuf_iterator<char>(ifs)),
                       std::istreambuf_iterator<char>());

      // Extract the timestamp from the filename (assuming it's formatted as in db_chats_new)
      std::string timestamp = entry.path().filename().string();

      // Add the data to the vectors
      timestamps.emplace_back(timestamp);
      chats.emplace_back(chat);
    }
  }

  // ---------------------------------------------------
  // Return data in Candid format specified in llama_cpp.did
  //
  // Create the 'chats' vec of records
  CandidTypeRecord r_out;
  r_out.append("timestamp", CandidTypeVecText{&timestamps});
  r_out.append("chat", CandidTypeVecText{&chats});
  CandidTypeVecRecord vr_out{r_out};

  // GetChatsRecord
  CandidTypeRecord get_chats_record;
  get_chats_record.append("chats", vr_out);

  // Wrap it into a Variant: GetChatsRecordResult
  ic_api.to_wire(CandidTypeVariant{"Ok", get_chats_record});
}

void chats_resume() {
  IC_API ic_api(CanisterUpdate{std::string(__func__)}, false);
  if (!is_caller_whitelisted(ic_api)) return;
  DB_CHATS_ACTIVE = true;

  std::cout << "llama_cpp: " << std::string(__func__)
            << " - DB_CHATS_ACTIVE set to true" << std::endl;

  CandidTypeRecord status_code_record;
  status_code_record.append("status_code", CandidTypeNat16{200});
  ic_api.to_wire(CandidTypeVariant{"Ok", status_code_record});
}

void chats_pause() {
  IC_API ic_api(CanisterUpdate{std::string(__func__)}, false);
  if (!is_caller_whitelisted(ic_api)) return;
  DB_CHATS_ACTIVE = false;

  std::cout << "llama_cpp: " << std::string(__func__)
            << " - DB_CHATS_ACTIVE set to false" << std::endl;

  CandidTypeRecord status_code_record;
  status_code_record.append("status_code", CandidTypeNat16{200});
  ic_api.to_wire(CandidTypeVariant{"Ok", status_code_record});
}
