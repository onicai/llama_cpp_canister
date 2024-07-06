// Main entry point for a native debug executable.
// Build it with: `icpp build-native` from the parent folder where 'icpp.toml' resides

#include "main.h"

#include <iostream>

#include "../src/run.h"

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
  // '(record { args = vec {"--model"; "stories260Ktok512.gguf"; "--prompt"; "Patrick loves ice-cream. On a hot day "; "--n-predict"; "600"; "--ctx-size"; "128"} })' ->
  // '(variant { Ok = record { status = 200 : nat16; output = "TODO" } })'
  mockIC.run_test(
      "run_query", run_query,
      "4449444c026c01dd9ad28304016d71010008072d2d6d6f64656c1673746f726965733236304b746f6b3531322e67677566082d2d70726f6d7074265061747269636b206c6f766573206963652d637265616d2e204f6e206120686f7420646179200b2d2d6e2d70726564696374033630300a2d2d6374782d73697a6503313238",
      "4449444c026b019cc201016c02b2ceef2f7a819e846471010000c80004544f444f",
      silent_on_trap, my_principal);

  // returns 1 if any tests failed
  return mockIC.test_summary();
}