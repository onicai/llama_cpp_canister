// Main entry point for a native debug executable.
// Build it with: `icpp build-native` from the parent folder where 'icpp.toml' resides

#include "main.h"

#include <iostream>

#include "../src/greet.h"

// The Mock IC
#include "mock_ic.h"

int main() {
  MockIC mockIC(true);

  // -----------------------------------------------------------------------------
  // Configs for the tests:

  // The pretend principals of the caller
  std::string my_principal{
      "expmt-gtxsw-inftj-ttabj-qhp5s-nozup-n3bbo-k7zvn-dg4he-knac3-lae"};
  std::string anonymous_principal{"2vxsx-fae"};

  bool silent_on_trap = true;

  // -----------------------------------------------------------------------------
  // '()' -> '("hello!")'
  mockIC.run_test("greet_0", greet_0, "4449444c0000",
                  "4449444c0001710668656c6c6f21", silent_on_trap, my_principal);

  // '()' -> '(variant { err = 401 : nat16})'
  // Unauthorized to call this API if not logged in
  mockIC.run_test("greet_0_auth err", greet_0_auth, "4449444c0000",
                  "4449444c016b01e58eb4027a0100009101", silent_on_trap,
                  anonymous_principal);

  // '()' -> '(variant { ok = "Hello expmt-gtxsw-inftj-ttabj-qhp5s-nozup-n3bbo-k7zvn-dg4he-knac3-lae" : text})'
  // Responds with your principal when logged in.
  mockIC.run_test(
      "greet_0_auth ok", greet_0_auth, "4449444c0000",
      "4449444c016b019cc201710100004548656c6c6f206578706d742d67747873772d696e66746a2d747461626a2d71687035732d6e6f7a75702d6e3362626f2d6b377a766e2d64673468652d6b6e6163332d6c6165",
      silent_on_trap, my_principal);

  // '()' -> '(2023)'
  mockIC.run_test("greet_1", greet_1, "4449444c0000", "4449444c00017ce70f",
                  silent_on_trap, my_principal);

  // Pretend caller has a principal "expmt-gtxsw-inftj-ttabj-qhp5s-nozup-n3bbo-k7zvn-dg4he-knac3-lae"
  // '("C++ Developer")' -> '("hello C++ Developer!\nYour principal is: expmt-gtxsw-inftj-ttabj-qhp5s-nozup-n3bbo-k7zvn-dg4he-knac3-lae")'
  mockIC.run_test(
      "greet_2", greet_2, "4449444c0001710d432b2b20446576656c6f706572",
      "4449444c0001716768656c6c6f20432b2b20446576656c6f706572210a596f7572207072696e636970616c2069733a206578706d742d67747873772d696e66746a2d747461626a2d71687035732d6e6f7a75702d6e3362626f2d6b377a766e2d64673468652d6b6e6163332d6c6165",
      silent_on_trap, my_principal);

  // '(record { "icpp version" = 1 : int; OS = "Linux" : text })' ->
  // '(record { "icpp Release Details" = "Version = 1 & Operating System = Linux" : text; "release year" = 2023 : int })'
  mockIC.run_test(
      "greet_3", greet_3,
      "4449444c016c02a48a0171d2fae7d70d7c0100054c696e757801",
      "4449444c016c02e3aa95930271b685be880f7c01002656657273696f6e203d20312026204f7065726174696e672053797374656d203d204c696e7578e70f",
      silent_on_trap, my_principal);

  // '(record { 6 = 42 : int; 9 = 43 : int }, record { 7 = 44 : int; 10 = 45 : int })' ->
  // '("Hello!", "Your secret numbers are:", 42 : int, 43 : int, 44 : int, 45 : int)'
  mockIC.run_test(
      "greet_4", greet_4, "4449444c026c02067c097c6c02077c0a7c0200012a2b2c2d",
      "4449444c000671717c7c7c7c0648656c6c6f2118596f757220736563726574206e756d62657273206172653a2a2b2c2d",
      silent_on_trap, my_principal);

  // '("{\"name\": \"AJ\"}")' -> '("{\"greet\":\"Hello AJ!\"}")'
  mockIC.run_test("greet_json", greet_json,
                  "4449444c0001710e7b226e616d65223a2022414a227d",
                  "4449444c000171157b226772656574223a2248656c6c6f20414a21227d",
                  silent_on_trap, my_principal);

  // returns 1 if any tests failed
  return mockIC.test_summary();
}