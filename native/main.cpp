// Main entry point for a native debug executable.
// Build it with: `icpp build-native` from the parent folder where 'icpp.toml' resides

#include "main.h"

#include <iostream>

#include "../src/health.h"
#include "../src/model.h"
#include "../src/ready.h"
#include "../src/run.h"
#include "../src/upload.h"
#include "../src/whoami.h"

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
  // The canister health & readiness checks
  // '()' -> '(variant { Ok = record { status_code = 200 : nat16} })'
  mockIC.run_test("health", health, "4449444c0000",
                  "4449444c026c019aa1b2f90c7a6b01bc8a0100010100c800",
                  silent_on_trap, anonymous_principal);

  // '()' -> '(variant { Err = record { Other = "Model not yet loaded"} })'
  mockIC.run_test(
      "ready", ready, "4449444c0000",
      "4449444c026b01b0ad8fcd0c716b01c5fed2010001010000144d6f64656c206e6f7420796574206c6f61646564",
      silent_on_trap, anonymous_principal);

  // -----------------------------------------------------------------------------

  // '()' -> '("expmt-gtxsw-inftj-ttabj-qhp5s-nozup-n3bbo-k7zvn-dg4he-knac3-lae")'
  mockIC.run_test(
      "whoami", whoami, "4449444c0000",
      "4449444c0001713f6578706d742d67747873772d696e66746a2d747461626a2d71687035732d6e6f7a75702d6e3362626f2d6b377a766e2d64673468652d6b6e6163332d6c6165",
      silent_on_trap, my_principal);

  // '()' -> '(variant { Err = record { Other = "Access Denied"} })'
  // Call with non controller principal must be denied
  mockIC.run_test(
      "reset_model Err", reset_model, "4449444c0000",
      "4449444c026b01b0ad8fcd0c716b01c5fed20100010100000d4163636573732044656e696564",
      silent_on_trap, anonymous_principal);

  // '()' -> '(variant { Ok = record { status_code = 200 : nat16} })'
  // Call with controller principal
  mockIC.run_test("reset_model", reset_model, "4449444c0000",
                  "4449444c026c019aa1b2f90c7a6b01bc8a0100010100c800",
                  silent_on_trap, my_principal);

  // -----------------------------------------------------------------------------
  // When running in a canister, first the `upload` endpoint will be called
  // We don't do that here, since after upload, it is just like reading from disk
  //
  // Call the load_model endpoint
  // '(record { args = vec {"--model"; "models/stories260Ktok512.gguf";} })' ->
  // '(variant { Ok = record { status = 200 : nat16; } })'
  mockIC.run_test(
      "load_model 260k", load_model,
      "4449444c026c01dd9ad28304016d71010002072d2d6d6f64656c1d6d6f64656c732f73746f726965733236304b746f6b3531322e67677566",
      "4449444c026b01bc8a01016c01b2ceef2f7a010000c800", silent_on_trap,
      my_principal);

  // '()' -> '(variant { Ok = record { status = 200 : nat16; } })'
  mockIC.run_test("ready OK", ready, "4449444c0000",
                  "4449444c026b01bc8a01016c01b2ceef2f7a010000c800",
                  silent_on_trap, anonymous_principal);

  // -----------------------------------------------------------------------------
  // '(record { args = vec {"--model"; "models/stories260Ktok512.gguf"; "--prompt"; "Patrick loves ice-cream. On a hot day "; "--n-predict"; "600"; "--ctx-size"; "128"} })' ->
  // '(variant { Ok = record { status = 200 : nat16; output = "TODO" } })'
  mockIC.run_test(
      "run_query 260k", run_query,
      "4449444c026c01dd9ad28304016d71010008072d2d6d6f64656c1d6d6f64656c732f73746f726965733236304b746f6b3531322e67677566082d2d70726f6d7074265061747269636b206c6f766573206963652d637265616d2e204f6e206120686f7420646179200b2d2d6e2d70726564696374033630300a2d2d6374782d73697a6503313238",
      "", silent_on_trap, my_principal);

  // -----------------------------------------------------------------------------
  // '(record { args = vec {"--model"; "models/stories260Ktok512.gguf"; "--prompt"; "Patrick loves ice-cream. On a hot day "; "--n-predict"; "600"; "--ctx-size"; "128"} })' ->
  // '(variant { Ok = record { status = 200 : nat16; output = "TODO" } })'
  mockIC.run_test(
      "run_update 260k", run_update,
      "4449444c026c01dd9ad28304016d71010008072d2d6d6f64656c1d6d6f64656c732f73746f726965733236304b746f6b3531322e67677566082d2d70726f6d7074265061747269636b206c6f766573206963652d637265616d2e204f6e206120686f7420646179200b2d2d6e2d70726564696374033630300a2d2d6374782d73697a6503313238",
      "", silent_on_trap, my_principal);

  // -----------------------------------------------------------------------------
  // '(record { args = vec {"--model"; "models/stories15Mtok4096.gguf"; "--prompt"; "Patrick loves ice-cream. On a hot day "; "--n-predict"; "600"; "--ctx-size"; "128"} })' ->
  // '(variant { Ok = record { status = 200 : nat16; output = "TODO" } })'
  mockIC.run_test(
      "run_query 15M", run_query,
      "4449444c026c01dd9ad28304016d71010008072d2d6d6f64656c1d6d6f64656c732f73746f726965733236304b746f6b3531322e67677566082d2d70726f6d7074265061747269636b206c6f766573206963652d637265616d2e204f6e206120686f7420646179200b2d2d6e2d70726564696374033630300a2d2d6374782d73697a6503313238",
      "", silent_on_trap, my_principal);

  // -----------------------------------------------------------------------------
  // '(record { args = vec {"--model"; "models/stories15Mtok4096.gguf"; "--prompt"; "Patrick loves ice-cream. On a hot day "; "--n-predict"; "600"; "--ctx-size"; "128"} })' ->
  // '(variant { Ok = record { status = 200 : nat16; output = "TODO" } })'
  mockIC.run_test(
      "run_update 15M", run_update,
      "4449444c026c01dd9ad28304016d71010008072d2d6d6f64656c1d6d6f64656c732f73746f726965733236304b746f6b3531322e67677566082d2d70726f6d7074265061747269636b206c6f766573206963652d637265616d2e204f6e206120686f7420646179200b2d2d6e2d70726564696374033630300a2d2d6374782d73697a6503313238",
      "", silent_on_trap, my_principal);

  // returns 1 if any tests failed
  return mockIC.test_summary();
}