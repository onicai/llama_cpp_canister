#include <iostream>

#include "../src/health.h"
#include "../src/logs.h"
#include "../src/max_tokens.h"
#include "../src/model.h"
#include "../src/promptcache.h"
#include "../src/ready.h"
#include "../src/run.h"
#include "../src/upload.h"
#include "../src/whoami.h"

// The Mock IC
#include "mock_ic.h"

void test_tiny_stories(MockIC &mockIC) {

  // -----------------------------------------------------------------------------
  // Configs for the tests:

  // The pretend principals of the caller
  std::string my_principal{
      "expmt-gtxsw-inftj-ttabj-qhp5s-nozup-n3bbo-k7zvn-dg4he-knac3-lae"};
  std::string anonymous_principal{"2vxsx-fae"};

  bool silent_on_trap = true;

  // -----------------------------------------------------------------------------
  // Pause logging
  // '()' -> '(variant { Ok = record { status_code = 200 : nat16; } })'
  mockIC.run_test(std::string(__func__) + ": " + "log_pause", log_pause,
                  "4449444c0000",
                  "4449444c026c019aa1b2f90c7a6b01bc8a0100010100c800",
                  silent_on_trap, my_principal);

  // -----------------------------------------------------------------------------
  // TinyStories models: text generation following a prompt
  std::vector<std::string> models = {"models/stories260Ktok512.gguf",
                                     "models/stories15Mtok4096.gguf"};

  for (const auto &model : models) {
    std::cout << "Loading & inferencing model: " << model << std::endl;

    std::string test_name;
    std::string candid_in;
    std::string candid_out;
    // -----------------------------------------------------------------------------
    // When running in a canister, first the `upload` endpoint will be called
    // We don't do that here, since after upload, it is just like reading from disk
    //
    // Call the load_model endpoint
    test_name = std::string(__func__) + ": " + "load_model - " + model;
    if (model == "models/stories260Ktok512.gguf") {
      // '(record { args = vec {"--model"; "models/stories260Ktok512.gguf";} })'
      candid_in =
          "4449444c026c01dd9ad28304016d71010002072d2d6d6f64656c1d6d6f64656c732f73746f726965733236304b746f6b3531322e67677566";
    } else if (
        model ==
        "models/stories15Mtok4096.gguf") { // '(record { args = vec {"--model"; "models/stories15Mtok4096.gguf";} })'
      candid_in =
          "4449444c026c01dd9ad28304016d71010002072d2d6d6f64656c1d6d6f64656c732f73746f7269657331354d746f6b343039362e67677566";
    }
    // '(variant { Ok = record { status_code = 200 : nat16; input=""; prompt_remaining=""; output="Model succesfully loaded into memory."; error=""; generated_eog=false : bool } })'
    candid_out =
        "4449444c026c06819e846471838fe5800671c897a79907719aa1b2f90c7adb92a2c90d71cdd9e6b30e7e6b01bc8a0100010100254d6f64656c2073756363657366756c6c79206c6f6164656420696e746f206d656d6f72792e0000c8000000";

    mockIC.run_test(test_name, load_model, candid_in, candid_out,
                    silent_on_trap, my_principal);

    // -----------------------------------------------------------------------------
    // Set max tokens to avoid hitting IC's instructions limit

    test_name = std::string(__func__) + ": " + "set_max_tokens - " + model;
    // '(record { max_tokens_query = 50 : nat64; max_tokens_update = 50 : nat64 })'
    candid_in =
        "4449444c016c02deb5daad0478f3a29d8e0778010032000000000000003200000000000000";
    // '(variant { Ok = record { status_code = 200 : nat16; } })'
    candid_out = "4449444c026c019aa1b2f90c7a6b01bc8a0100010100c800";

    mockIC.run_test(test_name, set_max_tokens, candid_in, candid_out,
                    silent_on_trap, my_principal);

    // -----------------------------------------------------------------------------
    // Get max tokens

    test_name = std::string(__func__) + ": " + "get_max_tokens - " + model;
    // '()'
    candid_in = "4449444c0000";
    // '(record { max_tokens_query = 50 : nat64; max_tokens_update = 50 : nat64;})'
    candid_out =
        "4449444c016c02deb5daad0478f3a29d8e0778010032000000000000003200000000000000";

    mockIC.run_test(test_name, get_max_tokens, candid_in, candid_out,
                    silent_on_trap, my_principal);

    // -----------------------------------------------------------------------------
    // Check readiness
    // '()' -> '(variant { Ok = record { status_code = 200 : nat16; } })'
    mockIC.run_test(std::string(__func__) + ": " + "ready OK - " + model, ready,
                    "4449444c0000",
                    "4449444c026c019aa1b2f90c7a6b01bc8a0100010100c800",
                    silent_on_trap, anonymous_principal);

    // Let's have two chats with this model
    for (int i = 0; i < 2; ++i) {

      if (i == 0) {
        // First chat with this model
        // -----------------------------------------------------------------------------
        // Remove the prompt-cache file if it exists
        // '(record { args = vec {"--prompt-cache"; "prompt.cache"} })' ->
        // '(variant { Ok = record { status_code = 200 : nat16; output = "Cache file .canister_cache/expmt-gtxsw-inftj-ttabj-qhp5s-nozup-n3bbo-k7zvn-dg4he-knac3-lae/sessions/prompt.cache deleted successfully"; input = ""; error=""; prompt_remaining=""; generated_eog=false : bool } })'
        mockIC.run_test(
            std::string(__func__) + ": " + "remove_prompt_cache " + model,
            remove_prompt_cache,
            "4449444c026c01dd9ad28304016d710100020e2d2d70726f6d70742d63616368650c70726f6d70742e6361636865",
            "", silent_on_trap, my_principal);

        // -----------------------------------------------------------------------------
        // Without log file & deterministic (temp=0)
        // Start a new chat
        // '(record { args = vec {"--prompt-cache"; "prompt.cache"} })' ->
        // '(variant { Ok = record { status_code = 200 : nat16; output = "Ready to start a new chat for cache file .canister_cache/expmt-gtxsw-inftj-ttabj-qhp5s-nozup-n3bbo-k7zvn-dg4he-knac3-lae/sessions/prompt.cache"; input = ""; error=""; prompt_remaining=""; generated_eog=false : bool } })'
        mockIC.run_test(
            std::string(__func__) + ": " + "new_chat " + std::to_string(i) +
                " - " + model,
            new_chat,
            "4449444c026c01dd9ad28304016d710100020e2d2d70726f6d70742d63616368650c70726f6d70742e6361636865",
            "4449444c026c06819e846471838fe5800671c897a79907719aa1b2f90c7adb92a2c90d71cdd9e6b30e7e6b01bc8a01000101008e01526561647920746f2073746172742061206e6577206368617420666f722063616368652066696c65202e63616e69737465725f63616368652f6578706d742d67747873772d696e66746a2d747461626a2d71687035732d6e6f7a75702d6e3362626f2d6b377a766e2d64673468652d6b6e6163332d6c61652f73657373696f6e732f70726f6d70742e63616368650000c8000000",
            silent_on_trap, my_principal);

        // -----------------------------------------------------------------------------
        // Generate tokens from prompt while saving everything to cache,
        // without re-reading the model !
        // '(record { args = vec {"--prompt-cache"; "prompt.cache"; "--prompt-cache-all"; "--samplers"; "temperature"; "--temp"; "0.0"; "-n"; "3"; "-p"; "Joe loves writing stories"} })'
        // -> ...
        if (model == "models/stories260Ktok512.gguf") {
          candid_out =
              "4449444c026c06819e846471838fe5800671c897a79907719aa1b2f90c7adb92a2c90d71cdd9e6b30e7e6b01bc8a0100010100072e204865206c691e204a6f65206c6f7665732077726974696e672073746f726965732e20486500c8000000";
        } else {
          candid_out =
              "4449444c026c06819e846471838fe5800671c897a79907719aa1b2f90c7adb92a2c90d71cdd9e6b30e7e6b01bc8a0100010100082e204865206861731e204a6f65206c6f7665732077726974696e672073746f726965732e20486500c8000000";
        }
        mockIC.run_test(
            std::string(__func__) + ": " + "run_update for chat " +
                std::to_string(i) + " - " + model,
            run_update,
            "4449444c026c01dd9ad28304016d7101000b0e2d2d70726f6d70742d63616368650c70726f6d70742e6361636865122d2d70726f6d70742d63616368652d616c6c0a2d2d73616d706c6572730b74656d7065726174757265062d2d74656d7003302e30022d6e0133022d70194a6f65206c6f7665732077726974696e672073746f72696573",
            candid_out, silent_on_trap, my_principal);

        // -----------------------------------------------------------------------------
        // Continue generating tokens while using & saving the cache, without re-reading the model
        // '(record { args = vec {"--prompt-cache"; "prompt.cache"; "--prompt-cache-all"; "--samplers"; "temperature"; "--temp"; "0.0"; "-n"; "3"; "-p"; ""} })' ->
        // -> ...
        if (model == "models/stories260Ktok512.gguf") {
          candid_out =
              "4449444c026c06819e846471838fe5800671c897a79907719aa1b2f90c7adb92a2c90d71cdd9e6b30e7e6b01bc8a010001010009206c696b656420746f24204a6f65206c6f7665732077726974696e672073746f726965732e204865206c696b656400c8000000";
        } else {
          candid_out =
              "4449444c026c06819e846471838fe5800671c897a79907719aa1b2f90c7adb92a2c90d71cdd9e6b30e7e6b01bc8a01000101000e206861732061207370656369616c24204a6f65206c6f7665732077726974696e672073746f726965732e20486520686173206100c8000000";
        }
        mockIC.run_test(
            std::string(__func__) + ": " + "run_update for chat " +
                std::to_string(i) + " continued - " + model,
            run_update,
            "4449444c026c01dd9ad28304016d7101000b0e2d2d70726f6d70742d63616368650c70726f6d70742e6361636865122d2d70726f6d70742d63616368652d616c6c0a2d2d73616d706c6572730b74656d7065726174757265062d2d74656d7003302e30022d6e0133022d7000",
            candid_out, silent_on_trap, my_principal);

      } else {
        // Second chat with this model
        // -----------------------------------------------------------------------------
        // copy "prompt.cache" file to "prompt-save.cache"
        // '(record { from = "prompt.cache"; to = "prompt-save.cache" })' ->
        // '(variant { Ok = record { status_code = 200 : nat16; } })'
        mockIC.run_test(
            std::string(__func__) + ": " + "copy_prompt_cache " + model,
            copy_prompt_cache,
            "4449444c016c02fbca0171eaca8a9e047101001170726f6d70742d736176652e63616368650c70726f6d70742e6361636865",
            "4449444c026c019aa1b2f90c7a6b01bc8a0100010100c800", silent_on_trap,
            my_principal);
        // -----------------------------------------------------------------------------
        // Remove "prompt.cache" file
        // '(record { args = vec {"--prompt-cache"; "prompt.cache"} })' ->
        // '(variant { Ok = record { status_code = 200 : nat16; output = "Cache file .canister_cache/expmt-gtxsw-inftj-ttabj-qhp5s-nozup-n3bbo-k7zvn-dg4he-knac3-lae/sessions/prompt.cache deleted successfully"; input = ""; error=""; prompt_remaining=""; generated_eog=false : bool } })'
        mockIC.run_test(
            std::string(__func__) + ": " + "remove_prompt_cache " + model,
            remove_prompt_cache,
            "4449444c026c01dd9ad28304016d710100020e2d2d70726f6d70742d63616368650c70726f6d70742e6361636865",
            "4449444c026c06819e846471838fe5800671c897a79907719aa1b2f90c7adb92a2c90d71cdd9e6b30e7e6b01bc8a0100010100850143616368652066696c65202e63616e69737465725f63616368652f6578706d742d67747873772d696e66746a2d747461626a2d71687035732d6e6f7a75702d6e3362626f2d6b377a766e2d64673468652d6b6e6163332d6c61652f73657373696f6e732f70726f6d70742e63616368652064656c65746564207375636365737366756c6c790000c8000000",
            silent_on_trap, my_principal);
        // -----------------------------------------------------------------------------
        // copy "prompt-save.cache" file back to "prompt.cache"
        // '(record { from = "prompt-save.cache"; to = "prompt.cache" })' ->
        // '(variant { Ok = record { status_code = 200 : nat16; } })'
        mockIC.run_test(
            std::string(__func__) + ": " + "copy_prompt_cache restore" + model,
            copy_prompt_cache,
            "4449444c016c02fbca0171eaca8a9e047101000c70726f6d70742e63616368651170726f6d70742d736176652e6361636865",
            "4449444c026c019aa1b2f90c7a6b01bc8a0100010100c800", silent_on_trap,
            my_principal);
        // -----------------------------------------------------------------------------
        // Remove "prompt-save.cache" file
        // '(record { args = vec {"--prompt-cache"; "prompt-save.cache"} })' ->
        // '(variant { Ok = record { status_code = 200 : nat16; output = "Cache file .canister_cache/expmt-gtxsw-inftj-ttabj-qhp5s-nozup-n3bbo-k7zvn-dg4he-knac3-lae/sessions/prompt-save.cache deleted successfully"; input = ""; error=""; prompt_remaining=""; generated_eog=false : bool } })'
        mockIC.run_test(
            std::string(__func__) + ": " +
                "remove_prompt_cache prompt-save.cache " + model,
            remove_prompt_cache,
            "4449444c026c01dd9ad28304016d710100020e2d2d70726f6d70742d63616368651170726f6d70742d736176652e6361636865",
            "", silent_on_trap, my_principal);
        // -----------------------------------------------------------------------------
        // With log file
        // Start a new chat, re-using the cache
        // '(record { args = vec {"--log-file"; "main.log"; "--prompt-cache"; "prompt.cache"} })' ->
        // '(variant { Ok = record { status_code = 200 : nat16; output = "Ready to start a new chat for cache file .canister_cache/expmt-gtxsw-inftj-ttabj-qhp5s-nozup-n3bbo-k7zvn-dg4he-knac3-lae/sessions/prompt.cache"; input = ""; error=""; prompt_remaining=""; generated_eog=false : bool } })'
        mockIC.run_test(
            std::string(__func__) + ": " + "new_chat " + std::to_string(i) +
                " - " + model,
            new_chat,
            "4449444c026c01dd9ad28304016d710100040a2d2d6c6f672d66696c65086d61696e2e6c6f670e2d2d70726f6d70742d63616368650c70726f6d70742e6361636865",
            "4449444c026c06819e846471838fe5800671c897a79907719aa1b2f90c7adb92a2c90d71cdd9e6b30e7e6b01bc8a01000101008e01526561647920746f2073746172742061206e6577206368617420666f722063616368652066696c65202e63616e69737465725f63616368652f6578706d742d67747873772d696e66746a2d747461626a2d71687035732d6e6f7a75702d6e3362626f2d6b377a766e2d64673468652d6b6e6163332d6c61652f73657373696f6e732f70726f6d70742e63616368650000c8000000",
            silent_on_trap, my_principal);

        // -----------------------------------------------------------------------------
        // Generate tokens from prompt while saving everything to cache,
        // without re-reading the model !
        // '(record { args = vec {"--log-file"; "main.log"; "--prompt-cache"; "prompt.cache"; "--prompt-cache-all"; "--samplers"; "temperature"; "--temp"; "0.0"; "-n"; "3"; "-p"; "Joe loves writing stories"} })'
        // -> ...
        if (model == "models/stories260Ktok512.gguf") {
          candid_out =
              "4449444c026c06819e846471838fe5800671c897a79907719aa1b2f90c7adb92a2c90d71cdd9e6b30e7e6b01bc8a0100010100072e204865206c691e204a6f65206c6f7665732077726974696e672073746f726965732e20486500c8000000";
        } else {
          candid_out =
              "4449444c026c06819e846471838fe5800671c897a79907719aa1b2f90c7adb92a2c90d71cdd9e6b30e7e6b01bc8a0100010100082e204865206861731e204a6f65206c6f7665732077726974696e672073746f726965732e20486500c8000000";
        }
        mockIC.run_test(
            std::string(__func__) + ": " + "run_update for chat " +
                std::to_string(i) + " - " + model,
            run_update,
            "4449444c026c01dd9ad28304016d7101000d0a2d2d6c6f672d66696c65086d61696e2e6c6f670e2d2d70726f6d70742d63616368650c70726f6d70742e6361636865122d2d70726f6d70742d63616368652d616c6c0a2d2d73616d706c6572730b74656d7065726174757265062d2d74656d7003302e30022d6e0133022d70194a6f65206c6f7665732077726974696e672073746f72696573",
            candid_out, silent_on_trap, my_principal);

        // -----------------------------------------------------------------------------
        // Continue generating tokens while using & saving the cache, without re-reading the model
        // '(record { args = vec {"--log-file"; "main.log"; "--prompt-cache"; "prompt.cache"; "--prompt-cache-all"; "--samplers"; "temperature"; "--temp"; "0.0"; "-n"; "3"; "-p"; ""} })' ->
        // -> ...
        if (model == "models/stories260Ktok512.gguf") {
          candid_out =
              "4449444c026c06819e846471838fe5800671c897a79907719aa1b2f90c7adb92a2c90d71cdd9e6b30e7e6b01bc8a010001010008206c69766564206124204a6f65206c6f7665732077726974696e672073746f726965732e204865206c6976656400c8000000";
        } else {
          candid_out =
              "4449444c026c06819e846471838fe5800671c897a79907719aa1b2f90c7adb92a2c90d71cdd9e6b30e7e6b01bc8a01000101000e206861732061207370656369616c24204a6f65206c6f7665732077726974696e672073746f726965732e20486520686173206100c8000000";
        }
        mockIC.run_test(
            std::string(__func__) + ": " + "run_update for chat " +
                std::to_string(i) + " continued - " + model,
            run_update,
            "4449444c026c01dd9ad28304016d7101000d0a2d2d6c6f672d66696c65086d61696e2e6c6f670e2d2d70726f6d70742d63616368650c70726f6d70742e6361636865122d2d70726f6d70742d63616368652d616c6c0a2d2d73616d706c6572730b74656d7065726174757265062d2d74656d7003302e30022d6e0133022d7000",
            candid_out, silent_on_trap, my_principal);

        // -----------------------------------------------------------------------------
        // Remove the prompt-cache files
        // '(record { args = vec {"--prompt-cache"; "prompt.cache"} })' ->
        // '(variant { Ok = record { status_code = 200 : nat16; output = "Cache file .canister_cache/expmt-gtxsw-inftj-ttabj-qhp5s-nozup-n3bbo-k7zvn-dg4he-knac3-lae/sessions/prompt.cache deleted successfully"; input = ""; error=""; prompt_remaining=""; generated_eog=false : bool } })'
        mockIC.run_test(
            std::string(__func__) + ": " + "remove_prompt_cache " +
                std::to_string(i) + " - " + model,
            remove_prompt_cache,
            "4449444c026c01dd9ad28304016d710100020e2d2d70726f6d70742d63616368650c70726f6d70742e6361636865",
            "4449444c026c06819e846471838fe5800671c897a79907719aa1b2f90c7adb92a2c90d71cdd9e6b30e7e6b01bc8a0100010100850143616368652066696c65202e63616e69737465725f63616368652f6578706d742d67747873772d696e66746a2d747461626a2d71687035732d6e6f7a75702d6e3362626f2d6b377a766e2d64673468652d6b6e6163332d6c61652f73657373696f6e732f70726f6d70742e63616368652064656c65746564207375636365737366756c6c790000c8000000",
            silent_on_trap, my_principal);

        // -----------------------------------------------------------------------------
        // Remove the log-file file if it exists
        // '(record { args = vec {"--log-file"; "main.log"} })' ->
        // '(variant { Ok = record { status_code = 200 : nat16; output = "Successfully removed log file: main.log"; input = ""; error=""; prompt_remaining=""; generated_eog=false : bool } })'
        mockIC.run_test(
            std::string(__func__) + ": " + "remove_log_file " + model,
            remove_log_file,
            "4449444c026c01dd9ad28304016d710100020a2d2d6c6f672d66696c65086d61696e2e6c6f67",
            "4449444c026c06819e846471838fe5800671c897a79907719aa1b2f90c7adb92a2c90d71cdd9e6b30e7e6b01bc8a0100010100275375636365737366756c6c792072656d6f766564206c6f672066696c653a206d61696e2e6c6f670000c8000000",
            silent_on_trap, my_principal);
      }
    }
  }

  // -----------------------------------------------------------------------------
  // Resume logging
  // '()' -> '(variant { Ok = record { status_code = 200 : nat16; } })'
  mockIC.run_test(std::string(__func__) + ": " + "log_resume", log_resume,
                  "4449444c0000",
                  "4449444c026c019aa1b2f90c7a6b01bc8a0100010100c800",
                  silent_on_trap, my_principal);
}