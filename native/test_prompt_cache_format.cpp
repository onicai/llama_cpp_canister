// Regression test: a prompt cache written by an older llama.cpp, whose stamp
// still names the current model and context layout, must be discarded (cold
// start), not fail on every call.
//
// v0.20.0 (llama.cpp b11476, LLAMA_SESSION_VERSION 9 -> 11) kept the v3 stamp
// of v0.19.1, so a v0.19.1 cache passed the stamp check, llama_state_load_file
// rejected it, and run_update returned "failed to load session file" until the
// cleanup timer removed the file. The stamp now carries llama.cpp's session
// version (prompt_cache_format()), so such a cache reads as stale.

#include "test_prompt_cache_format.h"

#include "../src/model.h"
#include "../src/promptcache.h"
#include "../src/run.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>

static const std::string FAKE_SESSION =
    "a session file written by an older llama.cpp";

void test_prompt_cache_format(MockIC &mockIC) {
  std::string controller{MOCKIC_CONTROLLER};
  bool silent_on_trap = true;

  // record { args = vec {"--model"; "models/stories260Ktok512.gguf"; "--no-warmup"} }
  mockIC.run_test(
      "test_prompt_cache_format: load stories260K", load_model,
      "4449444c026c01dd9ad28304016d71010003072d2d6d6f64656c1d6d6f64656c732f73746f726965733236304b746f6b3531322e676775660b2d2d6e6f2d7761726d7570",
      "", silent_on_trap, controller);

  // A cache + stamp as v0.19.1 leaves them: format line v3, but the current
  // model and context layout (so only the format line can reject it).
  const std::string dir = ".canister_cache/" + controller + "/sessions/";
  const std::string cache = dir + "stale_v3.cache";
  std::filesystem::create_directories(dir);
  {
    std::ofstream f(cache, std::ios::binary | std::ios::trunc);
    f << FAKE_SESSION;
  }
  {
    std::ofstream f(cache + ".icppfmt", std::ios::trunc);
    f << "llama_cpp_canister-prompt-cache-v3" << std::endl;
    f << prompt_cache_model_id() << std::endl;
    f << prompt_cache_layout_id() << std::endl;
  }

  // record { args = vec {"--prompt-cache"; "stale_v3.cache"; "--prompt-cache-all";
  //                      "-p"; "Joe loves writing stories"; "-n"; "2"} }
  mockIC.run_test(
      "test_prompt_cache_format: run_update on a v3 cache", run_update,
      "4449444c026c01dd9ad28304016d710100070e2d2d70726f6d70742d63616368650e7374616c655f76332e6361636865122d2d70726f6d70742d63616368652d616c6c022d70194a6f65206c6f7665732077726974696e672073746f72696573022d6e0132",
      "", silent_on_trap, controller);

  // A successful call replaces the fake bytes with a real session file and
  // rewrites the stamp; a failed one ("failed to load session file") returns
  // before saving, so the fake bytes stay.
  std::string content;
  {
    std::ifstream f(cache, std::ios::binary);
    content.assign(std::istreambuf_iterator<char>(f), {});
  }
  std::string format;
  {
    std::ifstream f(cache + ".icppfmt");
    std::getline(f, format);
  }
  if (content == FAKE_SESSION || format != prompt_cache_format()) {
    std::cout << "FAIL - test_prompt_cache_format: the old cache was not "
                 "discarded (run_update failed on it); stamp '"
              << format << "'" << std::endl;
    std::exit(1);
  }
  std::cout << "test_prompt_cache_format: old cache discarded, cold start, "
               "restamped as '"
            << format << "'" << std::endl;

  std::error_code ec;
  std::filesystem::remove(cache, ec);
  std::filesystem::remove(cache + ".icppfmt", ec);
}
