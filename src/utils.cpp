#include "utils.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdio.h>
#include <string>

#include "http.h"
#include "ic_api.h"

// NOTE the path: a bare #include "unicode.h" resolves to the TOKENIZER's header,
// because cpp_include_dirs lists .../fork/src before .../fork/common.
#include "common/unicode.h"

#include <map>

bool open_ifstream(const std::string &filename,
                   const std::ios_base::openmode &mode,
                   std::ifstream &if_stream, std::string &msg) {
  if_stream.open(filename.c_str(), mode);
  if (!if_stream.is_open()) {
    msg = "ERROR: failed to open ifstream for file " + filename;
    return false;
  }
  return true;
}

bool open_ofstream(const std::string &filename,
                   const std::ios_base::openmode &mode,
                   std::ofstream &of_stream, std::string &msg) {
  of_stream.open(filename.c_str(), mode);
  if (!of_stream.is_open()) {
    msg = "ERROR: failed to open ofstream for file " + filename;
    return false;
  }
  return true;
}

std::tuple<int, std::vector<char *>, std::unique_ptr<std::vector<std::string>>>
get_args_for_main(IC_API &ic_api) {
  // Use unique_ptr to ensure the lifetime of args
  auto args = std::make_unique<std::vector<std::string>>();
  CandidTypeRecord r_in;
  r_in.append("args", CandidTypeVecText{args.get()});
  ic_api.from_wire(r_in);

  // The first argv is always the program name
  args->insert(args->begin(), "llama_cpp_canister");

  // Construct argc
  int argc = args->size();

  // Construct argv
  std::vector<char *> argv(argc);
  for (int i = 0; i < argc; ++i) {
    argv[i] = &(*args)[i][0]; // Convert std::string to char*
  }

  // Print argc and argv
  // std::cout << "llama_cpp: " << std::string(__func__) << " - " << "argc: " << argc << std::endl;
  // for (int i = 0; i < argc; ++i) {
  //   std::cout << "llama_cpp: " << std::string(__func__) << " - " << "argv[" << i << "] = " << argv[i] << std::endl;
  // }

  return std::make_tuple(argc, std::move(argv), std::move(args));
}

bool my_create_directory(const std::filesystem::path &dir_path,
                         std::string &error_msg) {
  if (!dir_path.empty() && !std::filesystem::exists(dir_path)) {
    std::error_code ec;
    std::filesystem::create_directories(
        dir_path, ec); // Use the non-exception-throwing version
    if (ec) {
      error_msg = ec.message();
      return false;
    }
  }
  return true;
}

void send_output_record_result_error_to_wire(IC_API &ic_api,
                                             uint16_t http_status_code,
                                             const std::string &error_msg) {
  CandidTypeRecord r_out;
  r_out.append("status_code", CandidTypeNat16{http_status_code});
  r_out.append("conversation", CandidTypeText{""});
  r_out.append("output", CandidTypeText{""});
  r_out.append("error", CandidTypeText{error_msg});
  r_out.append("prompt_remaining", CandidTypeText{""});
  r_out.append("generated_eog", CandidTypeBool{false});
  ic_api.to_wire(CandidTypeVariant{"Err", r_out});
}

// U+FFFD REPLACEMENT CHARACTER, written out as bytes. Deliberately not built via
// common_unicode_cpt_to_utf8(), which throws (and a throw traps here).
static const char *const UTF8_REPLACEMENT = "\xEF\xBF\xBD";

size_t utf8_valid_prefix_len(const std::string &s) {
  size_t i = 0;
  while (i < s.size()) {
    const utf8_parse_result r = common_parse_utf8_codepoint(s, i);
    if (r.status == utf8_parse_result::INCOMPLETE) {
      // Ran out of input mid-sequence. By definition this can only happen at the
      // end, so `i` is the split point.
      break;
    }
    if (r.status == utf8_parse_result::INVALID) {
      // A genuinely bad byte, not a chunk boundary. Step over it so we never
      // stall carrying a byte that will never complete; utf8_sanitize() replaces
      // it before the prefix goes on the wire.
      i += 1;
      continue;
    }
    i += r.bytes_consumed;
  }
  return i;
}

std::string utf8_sanitize(const std::string &s) {
  std::string out;
  out.reserve(s.size());
  size_t i = 0;
  while (i < s.size()) {
    const utf8_parse_result r = common_parse_utf8_codepoint(s, i);
    if (r.status == utf8_parse_result::SUCCESS) {
      out.append(s, i, r.bytes_consumed);
      i += r.bytes_consumed;
    } else if (r.status == utf8_parse_result::INVALID) {
      out += UTF8_REPLACEMENT;
      i += 1;
    } else {
      // INCOMPLETE: a truncated tail with nothing left to read.
      out += UTF8_REPLACEMENT;
      break;
    }
  }
  return out;
}

// See utils.h for why this is exempt from reset_static_memory().
static std::map<std::string, std::string> g_utf8_tail_carry;

std::string utf8_carry_get(const std::string &session_key) {
  auto it = g_utf8_tail_carry.find(session_key);
  return it == g_utf8_tail_carry.end() ? std::string() : it->second;
}

void utf8_carry_set(const std::string &session_key, const std::string &tail) {
  if (tail.empty()) {
    g_utf8_tail_carry.erase(session_key);
  } else {
    g_utf8_tail_carry[session_key] = tail;
  }
}

void utf8_carry_clear(const std::string &session_key) {
  g_utf8_tail_carry.erase(session_key);
}
