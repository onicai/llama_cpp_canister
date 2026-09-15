// Native tests for the UTF-8 helpers that keep Candid `text` fields valid.
//
// These call the helpers DIRECTLY (no candid round-trip), like
// test_cycle_balance.cpp / test_cache_cleanup.cpp do, because they are plain
// functions rather than canister endpoints.
//
// Background: run_update returns generation in max_tokens-sized chunks, and
// byte-level BPE encodes Devanagari as byte tokens, so a chunk boundary lands
// mid-codepoint. Candid `text` must be valid UTF-8 or the CALLER traps while
// decoding (IC0503 in Motoko, uncatchable). See TMP-HANDOVER-hindi-utf8-chunking.md.

#include "test_utf8.h"

#include "../src/health.h"
#include "../src/utils.h"

#include "ic_api.h"
#include "mock_ic.h"

#include <iostream>
#include <string>

namespace {

int expect_eq_size(const char *label, size_t actual, size_t expected) {
  if (actual != expected) {
    std::cout << "FAIL: " << label << " expected " << expected << ", got "
              << actual << '\n';
    return 1;
  }
  std::cout << "PASS: " << label << " == " << actual << '\n';
  return 0;
}

int expect_eq_str(const char *label, const std::string &actual,
                  const std::string &expected) {
  if (actual != expected) {
    std::cout << "FAIL: " << label << " expected '" << expected << "', got '"
              << actual << "'\n";
    return 1;
  }
  std::cout << "PASS: " << label << '\n';
  return 0;
}

// U+0927 DEVANAGARI LETTER DHA - the first character of धैर्य, 3 bytes.
const std::string DHA = "\xE0\xA4\xA7";
const std::string REPL = "\xEF\xBF\xBD"; // U+FFFD

} // namespace

void test_utf8(MockIC &mockIC) {
  std::string controller_principal{MOCKIC_CONTROLLER};
  bool silent_on_trap = true;
  const std::string EMPTY_INPUT = "4449444c0000"; // didc encode '()'
  int extra_failures = 0;

  std::cout << "\n========== test_utf8 ==========\n";

  // -------------------------------------------------------------------------
  // utf8_valid_prefix_len: where may we cut?
  extra_failures +=
      expect_eq_size("prefix: empty", utf8_valid_prefix_len(""), 0);
  extra_failures += expect_eq_size("prefix: pure ASCII is never split",
                                   utf8_valid_prefix_len("hello"), 5);
  extra_failures += expect_eq_size("prefix: complete 2-byte (c3 a9)",
                                   utf8_valid_prefix_len("\xC3\xA9"), 2);
  extra_failures += expect_eq_size("prefix: complete 3-byte (Devanagari)",
                                   utf8_valid_prefix_len(DHA), 3);
  extra_failures +=
      expect_eq_size("prefix: complete 4-byte (emoji)",
                     utf8_valid_prefix_len("\xF0\x9F\x98\x80"), 4);

  // The cases that actually bit us: a 3-byte codepoint cut after 1 and 2 bytes.
  extra_failures += expect_eq_size("prefix: 3-byte truncated after 1 byte",
                                   utf8_valid_prefix_len("ab\xE0"), 2);
  extra_failures += expect_eq_size("prefix: 3-byte truncated after 2 bytes",
                                   utf8_valid_prefix_len("ab\xE0\xA4"), 2);
  extra_failures += expect_eq_size("prefix: complete char then truncated next",
                                   utf8_valid_prefix_len(DHA + "\xE0\xA4"), 3);

  // A lone continuation byte is NOT a boundary - it is garbage. The helper must
  // step over it so the carry can never stall on a byte that never completes.
  extra_failures += expect_eq_size("prefix: lone continuation byte steps over",
                                   utf8_valid_prefix_len("\xAA"), 1);
  extra_failures += expect_eq_size("prefix: invalid lead byte steps over",
                                   utf8_valid_prefix_len("\xFF"), 1);

  // -------------------------------------------------------------------------
  // utf8_sanitize: identity on valid input, U+FFFD otherwise.
  extra_failures += expect_eq_str("sanitize: empty", utf8_sanitize(""), "");
  extra_failures += expect_eq_str("sanitize: ASCII unchanged",
                                  utf8_sanitize("hello"), "hello");
  extra_failures += expect_eq_str("sanitize: valid Devanagari unchanged",
                                  utf8_sanitize(DHA), DHA);
  extra_failures += expect_eq_str("sanitize: truncated tail -> U+FFFD",
                                  utf8_sanitize("ab\xE0\xA4"), "ab" + REPL);
  extra_failures += expect_eq_str("sanitize: orphan continuation -> U+FFFD",
                                  utf8_sanitize("\xAA" + DHA), REPL + DHA);

  // -------------------------------------------------------------------------
  // The property that matters: split + rejoin loses NOTHING.
  //
  // Simulates two successive run_update chunks whose boundary falls inside a
  // codepoint: chunk N is cut at the prefix, the tail is carried, and chunk N+1
  // is prepended with it. The concatenation must equal the original bytes.
  {
    const std::string original = "a" + DHA + DHA + "b";
    for (size_t cut = 0; cut <= original.size(); ++cut) {
      const std::string chunk1 = original.substr(0, cut);
      const std::string chunk2 = original.substr(cut);

      const size_t n = utf8_valid_prefix_len(chunk1);
      const std::string sent1 = chunk1.substr(0, n);
      const std::string carry = chunk1.substr(n);

      const std::string full2 = carry + chunk2;
      const size_t n2 = utf8_valid_prefix_len(full2);
      const std::string sent2 = full2.substr(0, n2);

      const std::string label =
          "roundtrip: cut at " + std::to_string(cut) + " loses no bytes";
      extra_failures += expect_eq_str(label.c_str(), sent1 + sent2, original);
      // Whatever was sent must itself be valid, or the caller traps.
      extra_failures += expect_eq_str(
          ("roundtrip: cut at " + std::to_string(cut) + " sent1 is valid")
              .c_str(),
          utf8_sanitize(sent1), sent1);
    }
  }

  std::cout << "test_utf8 extra_failures: " << extra_failures
            << "\n========================================\n\n";
  if (extra_failures > 0) {
    // Surface the failure count via mockIC's pass/fail summary: the expect_*
    // helpers above do NOT flow through run_test, so without this a regression
    // would print FAIL and still exit 0.
    mockIC.run_test(
        "test_utf8: extra_failures detected (see PASS/FAIL log above)", health,
        EMPTY_INPUT, "DELIBERATE_FAIL_TO_RAISE_ALARM", silent_on_trap,
        controller_principal);
  }
}
