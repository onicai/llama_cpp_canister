#include <iostream>

#include "../src/db_chats.h"
#include "../src/health.h"
#include "../src/logs.h"
#include "../src/max_tokens.h"
#include "../src/model.h"
#include "../src/ready.h"
#include "../src/run.h"
#include "../src/upload.h"
#include "../src/whoami.h"

// The Mock IC
#include "mock_ic.h"

void test_qwen2(MockIC &mockIC) {

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
  std::string model =
      "models/Qwen/Qwen2.5-0.5B-Instruct-GGUF/qwen2.5-0.5b-instruct-q8_0.gguf";

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
  // '(record { args = vec {"--model"; "models/Qwen/Qwen2.5-0.5B-Instruct-GGUF/qwen2.5-0.5b-instruct-q8_0.gguf";} })'
  candid_in =
      "4449444c026c01dd9ad28304016d71010002072d2d6d6f64656c466d6f64656c732f5177656e2f5177656e322e352d302e35422d496e7374727563742d474755462f7177656e322e352d302e35622d696e7374727563742d71385f302e67677566";
  // '(variant { Ok = record { status_code = 200 : nat16; input=""; prompt_remaining=""; output="Model succesfully loaded into memory."; error=""; generated_eog=false : bool } })'
  candid_out =
      "4449444c026c06819e846471838fe5800671c897a79907719aa1b2f90c7adb92a2c90d71cdd9e6b30e7e6b01bc8a0100010100254d6f64656c2073756363657366756c6c79206c6f6164656420696e746f206d656d6f72792e0000c8000000";

  mockIC.run_test(test_name, load_model, candid_in, candid_out, silent_on_trap,
                  my_principal);

  // -----------------------------------------------------------------------------
  // Set max tokens to avoid hitting IC's instructions limit

  test_name = std::string(__func__) + ": " + "set_max_tokens - " + model;
  // '(record { max_tokens_query = 12 : nat64; max_tokens_update = 12 : nat64 })'
  candid_in =
      "4449444c016c02deb5daad0478f3a29d8e077801000c000000000000000c00000000000000";
  // '(variant { Ok = record { status_code = 200 : nat16; } })'
  candid_out = "4449444c026c019aa1b2f90c7a6b01bc8a0100010100c800";

  mockIC.run_test(test_name, set_max_tokens, candid_in, candid_out,
                  silent_on_trap, my_principal);

  // -----------------------------------------------------------------------------
  // Get max tokens

  test_name = std::string(__func__) + ": " + "get_max_tokens - " + model;
  // '()'
  candid_in = "4449444c0000";
  // '(record { max_tokens_query = 12 : nat64; max_tokens_update = 12 : nat64;})'
  candid_out =
      "4449444c016c02deb5daad0478f3a29d8e077801000c000000000000000c00000000000000";

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
    // Start a new chat
    // '(record { args = vec {"--prompt-cache"; "prompt.cache"} })' ->
    // '(variant { Ok = record { status_code = 200 : nat16; output = "Ready to start a new chat for cache file .canister_cache/expmt-gtxsw-inftj-ttabj-qhp5s-nozup-n3bbo-k7zvn-dg4he-knac3-lae/sessions/prompt.cache"; input = ""; error=""; prompt_remaining=""; generated_eog=false : bool } })'
    mockIC.run_test(
        std::string(__func__) + ": " + "new_chat " + std::to_string(i) + " - " +
            model,
        new_chat,
        "4449444c026c01dd9ad28304016d710100020e2d2d70726f6d70742d63616368650c70726f6d70742e6361636865",
        "4449444c026c06819e846471838fe5800671c897a79907719aa1b2f90c7adb92a2c90d71cdd9e6b30e7e6b01bc8a01000101008e01526561647920746f2073746172742061206e6577206368617420666f722063616368652066696c65202e63616e69737465725f63616368652f6578706d742d67747873772d696e66746a2d747461626a2d71687035732d6e6f7a75702d6e3362626f2d6b377a766e2d64673468652d6b6e6163332d6c61652f73657373696f6e732f70726f6d70742e63616368650000c8000000",
        silent_on_trap, my_principal);

    // -----------------------------------------------------------------------------
    // -sp  : special token output enabled
    // '(record { args = vec {"--prompt-cache"; "prompt.cache"; "--prompt-cache-all"; "-sp"; "-n"; "512"; "-p"; "<|im_start|>system\nYou are a helpful assistant.<|im_end|>\n<|im_start|>user\nExplain Large Language Models.<|im_end|>\n<|im_start|>assistant\n"} })' ->
    // -> '(variant { Ok = record { status_code = 200 : nat16; error = ""; output = ""; input = "<|im_start|>system\nYou are a helpful assistant.<|im_end|>\n<|im_start|>" ; prompt_remaining = "user\nExplain Large Language Models.<|im_end|>\n<|im_start|>assistant\n"; generated_eog=false : bool} })'
    mockIC.run_test(
        std::string(__func__) + ": " + "run_update prompt step 1 for chat " +
            std::to_string(i) + " - " + model,
        run_update,
        "4449444c026c01dd9ad28304016d710100080e2d2d70726f6d70742d63616368650c70726f6d70742e6361636865122d2d70726f6d70742d63616368652d616c6c032d7370022d6e03353132022d708a013c7c696d5f73746172747c3e73797374656d0a596f752061726520612068656c7066756c20617373697374616e742e3c7c696d5f656e647c3e0a3c7c696d5f73746172747c3e757365720a4578706c61696e204c61726765204c616e6775616765204d6f64656c732e3c7c696d5f656e647c3e0a3c7c696d5f73746172747c3e617373697374616e740a",
        "4449444c026c06819e846471838fe5800671c897a79907719aa1b2f90c7adb92a2c90d71cdd9e6b30e7e6b01bc8a010001010000463c7c696d5f73746172747c3e73797374656d0a596f752061726520612068656c7066756c20617373697374616e742e3c7c696d5f656e647c3e0a3c7c696d5f73746172747c3e00c80044757365720a4578706c61696e204c61726765204c616e6775616765204d6f64656c732e3c7c696d5f656e647c3e0a3c7c696d5f73746172747c3e617373697374616e740a00",
        silent_on_trap, my_principal);

    // -----------------------------------------------------------------------------
    // -sp  : special token output enabled
    // '(record { args = vec {"--prompt-cache"; "prompt.cache"; "--prompt-cache-all"; "-sp"; "-n"; "512"; "-p"; "<|im_start|>system\nYou are a helpful assistant.<|im_end|>\n<|im_start|>user\nExplain Large Language Models.<|im_end|>\n<|im_start|>assistant\n"} })' ->
    // -> '(variant { Ok = record { status_code = 200 : nat16; error = ""; output = ""; input = "<|im_start|>system\nYou are a helpful assistant.<|im_end|>\n<|im_start|>user\nExplain Large Language Models.<|im_end|>\n<|im_start|>assistant"; prompt_remaining = "\n";} generated_eog=false : bool})'
    mockIC.run_test(
        std::string(__func__) + ": " + "run_update prompt step 2 for chat " +
            std::to_string(i) + " - " + model,
        run_update,
        "4449444c026c01dd9ad28304016d710100080e2d2d70726f6d70742d63616368650c70726f6d70742e6361636865122d2d70726f6d70742d63616368652d616c6c032d7370022d6e03353132022d708a013c7c696d5f73746172747c3e73797374656d0a596f752061726520612068656c7066756c20617373697374616e742e3c7c696d5f656e647c3e0a3c7c696d5f73746172747c3e757365720a4578706c61696e204c61726765204c616e6775616765204d6f64656c732e3c7c696d5f656e647c3e0a3c7c696d5f73746172747c3e617373697374616e740a",
        "4449444c026c06819e846471838fe5800671c897a79907719aa1b2f90c7adb92a2c90d71cdd9e6b30e7e6b01bc8a01000101000089013c7c696d5f73746172747c3e73797374656d0a596f752061726520612068656c7066756c20617373697374616e742e3c7c696d5f656e647c3e0a3c7c696d5f73746172747c3e757365720a4578706c61696e204c61726765204c616e6775616765204d6f64656c732e3c7c696d5f656e647c3e0a3c7c696d5f73746172747c3e617373697374616e7400c800010a00",
        silent_on_trap, my_principal);

    // -----------------------------------------------------------------------------
    // '(record { args = vec {"--prompt-cache"; "prompt.cache"; "--prompt-cache-all"; "-sp"; "-n"; "512"; "-p"; "<|im_start|>system\nYou are a helpful assistant.<|im_end|>\n<|im_start|>user\nExplain Large Language Models.<|im_end|>\n<|im_start|>assistant\n"} })' ->
    // -> can no longer check it, because the LLM generated tokens
    mockIC.run_test(
        std::string(__func__) + ": " + "run_update prompt step 3 for chat " +
            std::to_string(i) + " - " + model,
        run_update,
        "4449444c026c01dd9ad28304016d710100080e2d2d70726f6d70742d63616368650c70726f6d70742e6361636865122d2d70726f6d70742d63616368652d616c6c032d7370022d6e03353132022d708a013c7c696d5f73746172747c3e73797374656d0a596f752061726520612068656c7066756c20617373697374616e742e3c7c696d5f656e647c3e0a3c7c696d5f73746172747c3e757365720a4578706c61696e204c61726765204c616e6775616765204d6f64656c732e3c7c696d5f656e647c3e0a3c7c696d5f73746172747c3e617373697374616e740a",
        "", silent_on_trap, my_principal);

    // -----------------------------------------------------------------------------
    // Once there is no prompt_remaining, it is totally ok to send an empty prompt, and just let it generate new tokens
    // '(record { args = vec {"--prompt-cache"; "prompt.cache"; "--prompt-cache-all"; "-sp"; "-n"; "512"; "-p"; ""} })' ->
    // -> can no longer check it, because the LLM generated tokens
    mockIC.run_test(
        std::string(__func__) + ": " + "run_update prompt step 4 for chat " +
            std::to_string(i) + " - " + model,
        run_update,
        "4449444c026c01dd9ad28304016d710100080e2d2d70726f6d70742d63616368650c70726f6d70742e6361636865122d2d70726f6d70742d63616368652d616c6c032d7370022d6e03353132022d7000",
        "", silent_on_trap, my_principal);
  }

  // -----------------------------------------------------------------------------
  // Retrieve the saved chats
  // '()' -> '(variant { Ok = record { chats = vec {record { id="id1"; chat="chat1";}; record { id="id2"; chat="chat2";}; } } })'
  // Cannot test it because values of chat are non-deterministic
  // Note that the pytest is verifying it in more detail
  mockIC.run_test(std::string(__func__) + ": " + "get_chats - " + model,
                  get_chats, "4449444c0000", "", silent_on_trap, my_principal);

  // -----------------------------------------------------------------------------
  // Remove the prompt-cache file if it exists
  // '(record { args = vec {"--prompt-cache"; "prompt.cache"} })' ->
  // '(variant { Ok = record { status_code = 200 : nat16; output = "Cache file .canister_cache/expmt-gtxsw-inftj-ttabj-qhp5s-nozup-n3bbo-k7zvn-dg4he-knac3-lae/sessions/prompt.cache deleted successfully"; input = ""; error=""; prompt_remaining=""; generated_eog=false : bool } })'
  mockIC.run_test(
      std::string(__func__) + ": " + "remove_prompt_cache " + model,
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

  // -----------------------------------------------------------------------------
  // Resume logging
  // '()' -> '(variant { Ok = record { status_code = 200 : nat16; } })'
  mockIC.run_test(std::string(__func__) + ": " + "log_resume", log_resume,
                  "4449444c0000",
                  "4449444c026c019aa1b2f90c7a6b01bc8a0100010100c800",
                  silent_on_trap, my_principal);
}