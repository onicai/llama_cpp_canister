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

  std::vector<std::string> models = {"models/stories260Ktok512.gguf", "models/stories15Mtok4096.gguf"};
    
  for (const auto& model : models) {
    std::cout << "Loading & inferencing model: " << model << std::endl;

    std::string candid_in;
    std::string candid_out;
    // -----------------------------------------------------------------------------
    // When running in a canister, first the `upload` endpoint will be called
    // We don't do that here, since after upload, it is just like reading from disk
    //
    // Call the load_model endpoint    
    if (model == "models/stories260Ktok512.gguf") {
        // '(record { args = vec {"--model"; "models/stories260Ktok512.gguf";} })'
        candid_in = "4449444c026c01dd9ad28304016d71010002072d2d6d6f64656c1d6d6f64656c732f73746f726965733236304b746f6b3531322e67677566";
    } else if (model == "models/stories15Mtok4096.gguf") {\
        // '(record { args = vec {"--model"; "models/stories15Mtok4096.gguf";} })'
        candid_in = "4449444c026c01dd9ad28304016d71010002072d2d6d6f64656c1d6d6f64656c732f73746f7269657331354d746f6b343039362e67677566";
    }
    // candid_in ->
    // '(variant { Ok = record { status_code = 200 : nat16; } })'
    mockIC.run_test(
        "load_model - " + model, load_model,
        candid_in,
        "4449444c026c019aa1b2f90c7a6b01bc8a0100010100c800", silent_on_trap,
        my_principal);

    // -----------------------------------------------------------------------------
    // Check readiness
    // '()' -> '(variant { Ok = record { status_code = 200 : nat16; } })'
    mockIC.run_test("ready OK - " + model, ready, "4449444c0000",
                    "4449444c026c019aa1b2f90c7a6b01bc8a0100010100c800",
                    silent_on_trap, anonymous_principal);

    // Let's have two chats with this model
    for (int i = 0; i < 2; ++i) {
        // -----------------------------------------------------------------------------
        // Start a new chat, which will remove the prompt-cache file if it exists
        // '(record { args = vec {"--prompt-cache"; "my_cache/prompt.cache"} })' ->
        // '(variant { Ok = record { status_code = 200 : nat16; output = "Cache .canister_cache/expmt-gtxsw-inftj-ttabj-qhp5s-nozup-n3bbo-k7zvn-dg4he-knac3-lae/my_cache/prompt.cache not found. Nothing to delete." } })'
        mockIC.run_test(
            "new_chat " + std::to_string(i) + " - " + model, new_chat,
            "4449444c026c01dd9ad28304016d710100020e2d2d70726f6d70742d6361636865156d795f63616368652f70726f6d70742e6361636865",
            "4449444c026c02b2ceef2f7a819e8464716b01bc8a0100010100c8008e01526561647920746f2073746172742061206e6577206368617420666f722063616368652066696c65202e63616e69737465725f63616368652f6578706d742d67747873772d696e66746a2d747461626a2d71687035732d6e6f7a75702d6e3362626f2d6b377a766e2d64673468652d6b6e6163332d6c61652f6d795f63616368652f70726f6d70742e6361636865", silent_on_trap, my_principal);

        // -----------------------------------------------------------------------------
        // Generate tokens from prompt while saving everything to cache, 
        // without re-reading the model !
        // '(record { args = vec {"--prompt-cache"; "my_cache/prompt.cache"; "--prompt-cache-all"; "--samplers"; "top_p"; "--temp"; "0.1"; "--top-p"; "0.9"; "-n"; "20"; "-p"; "Joe loves writing stories"} })' ->
        // '(variant { Ok = record { status_code = 200 : nat16; output = "TODO" } })'
        mockIC.run_test(
            "run_update for chat " + std::to_string(i) + " - " + model, run_update,
            "4449444c026c01dd9ad28304016d7101000d0e2d2d70726f6d70742d6361636865156d795f63616368652f70726f6d70742e6361636865122d2d70726f6d70742d63616368652d616c6c0a2d2d73616d706c65727305746f705f70062d2d74656d7003302e31072d2d746f702d7003302e39022d6e023230022d70194a6f65206c6f7665732077726974696e672073746f72696573",
            "", silent_on_trap, my_principal);

        // -----------------------------------------------------------------------------
        // Continue generating tokens while using & saving the cache, without re-reading the model
        // '(record { args = vec {"--prompt-cache"; "my_cache/prompt.cache"; "--prompt-cache-all"; "--samplers"; "top_p"; "--temp"; "0.1"; "--top-p"; "0.9"; "-n"; "20"; "-p"; ""} })' ->
        // '(variant { Ok = record { status_code = 200 : nat16; output = "TODO" } })'
        mockIC.run_test(
            "run_update for chat " + std::to_string(i) + " continued - " + model, run_update,
            "4449444c026c01dd9ad28304016d7101000d0e2d2d70726f6d70742d6361636865156d795f63616368652f70726f6d70742e6361636865122d2d70726f6d70742d63616368652d616c6c0a2d2d73616d706c65727305746f705f70062d2d74656d7003302e31072d2d746f702d7003302e39022d6e023230022d7000",
            "", silent_on_trap, my_principal);
    }
  }

  // #############################################################################
  // We also support running in a more direct mode, where you:
  // (-) specify the model
  // (-) don't use a cache file
  //
  // This is not very common though, but perhaps in the future it is useful once
  // we can use query calls.
  // #############################################################################


  // -----------------------------------------------------------------------------
  // '(record { args = vec {"--model"; "models/stories260Ktok512.gguf"; "--prompt"; "Patrick loves ice-cream. On a hot day "; "--n-predict"; "600"; "--ctx-size"; "128"} })' ->
  // '(variant { Ok = record { status_code = 200 : nat16; output = "TODO" } })'
  mockIC.run_test(
      "run_query 260k", run_query,
      "4449444c026c01dd9ad28304016d71010008072d2d6d6f64656c1d6d6f64656c732f73746f726965733236304b746f6b3531322e67677566082d2d70726f6d7074265061747269636b206c6f766573206963652d637265616d2e204f6e206120686f7420646179200b2d2d6e2d70726564696374033630300a2d2d6374782d73697a6503313238",
      "", silent_on_trap, my_principal);

  // -----------------------------------------------------------------------------
  // '(record { args = vec {"--model"; "models/stories260Ktok512.gguf"; "--prompt"; "Patrick loves ice-cream. On a hot day "; "--n-predict"; "600"; "--ctx-size"; "128"} })' ->
  // '(variant { Ok = record { status_code = 200 : nat16; output = "TODO" } })'
  mockIC.run_test(
      "run_update 260k", run_update,
      "4449444c026c01dd9ad28304016d71010008072d2d6d6f64656c1d6d6f64656c732f73746f726965733236304b746f6b3531322e67677566082d2d70726f6d7074265061747269636b206c6f766573206963652d637265616d2e204f6e206120686f7420646179200b2d2d6e2d70726564696374033630300a2d2d6374782d73697a6503313238",
      "", silent_on_trap, my_principal);

  // -----------------------------------------------------------------------------
  // '(record { args = vec {"--model"; "models/stories15Mtok4096.gguf"; "--prompt"; "Patrick loves ice-cream. On a hot day "; "--n-predict"; "600"; "--ctx-size"; "128"} })' ->
  // '(variant { Ok = record { status_code = 200 : nat16; output = "TODO" } })'
  mockIC.run_test(
      "run_query 15M", run_query,
      "4449444c026c01dd9ad28304016d71010008072d2d6d6f64656c1d6d6f64656c732f73746f726965733236304b746f6b3531322e67677566082d2d70726f6d7074265061747269636b206c6f766573206963652d637265616d2e204f6e206120686f7420646179200b2d2d6e2d70726564696374033630300a2d2d6374782d73697a6503313238",
      "", silent_on_trap, my_principal);

  // -----------------------------------------------------------------------------
  // '(record { args = vec {"--model"; "models/stories15Mtok4096.gguf"; "--prompt"; "Patrick loves ice-cream. On a hot day "; "--n-predict"; "600"; "--ctx-size"; "128"} })' ->
  // '(variant { Ok = record { status_code = 200 : nat16; output = "TODO" } })'
  mockIC.run_test(
      "run_update 15M", run_update,
      "4449444c026c01dd9ad28304016d71010008072d2d6d6f64656c1d6d6f64656c732f73746f726965733236304b746f6b3531322e67677566082d2d70726f6d7074265061747269636b206c6f766573206963652d637265616d2e204f6e206120686f7420646179200b2d2d6e2d70726564696374033630300a2d2d6374782d73697a6503313238",
      "", silent_on_trap, my_principal);

  // returns 1 if any tests failed
  return mockIC.test_summary();
}