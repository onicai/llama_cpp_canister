#include <iostream>

#include "../src/health.h"
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
  test_name = "load_model - " + model;
  // '(record { args = vec {"--model"; "models/Qwen/Qwen2.5-0.5B-Instruct-GGUF/qwen2.5-0.5b-instruct-q8_0.gguf";} })'
  candid_in =
      "4449444c026c01dd9ad28304016d71010002072d2d6d6f64656c466d6f64656c732f5177656e2f5177656e322e352d302e35422d496e7374727563742d474755462f7177656e322e352d302e35622d696e7374727563742d71385f302e67677566";
  // '(variant { Ok = record { status_code = 200 : nat16; input=""; prompt_remaining=""; output="Model succesfully loaded into memory."; error="" } })'
  candid_out =
      "4449444c026c05819e846471c897a79907718a88f7f00b719aa1b2f90c7adb92a2c90d716b01bc8a0100010100254d6f64656c2073756363657366756c6c79206c6f6164656420696e746f206d656d6f72792e0000c80000";

  mockIC.run_test(test_name, load_model, candid_in, candid_out, silent_on_trap,
                  my_principal);

  // -----------------------------------------------------------------------------
  // Set max tokens to avoid hitting IC's instructions limit

  test_name = "set_max_tokens - " + model;
  // '(record { max_tokens_query = 12 : nat64; max_tokens_update = 12 : nat64 })'
  candid_in =
      "4449444c016c02deb5daad0478f3a29d8e077801000c000000000000000c00000000000000";
  // '(variant { Ok = record { status_code = 200 : nat16; } })'
  candid_out = "4449444c026c019aa1b2f90c7a6b01bc8a0100010100c800";

  mockIC.run_test(test_name, set_max_tokens, candid_in, candid_out,
                  silent_on_trap, my_principal);

  // -----------------------------------------------------------------------------
  // Get max tokens to avoid hitting IC's instructions limit

  test_name = "get_max_tokens - " + model;
  // '()'
  candid_in = "4449444c0000";
  // '(record { max_tokens_query = 12 : nat64; max_tokens_update = 12 : nat64 })'
  candid_out =
      "4449444c016c02deb5daad0478f3a29d8e077801000c000000000000000c00000000000000";

  mockIC.run_test(test_name, get_max_tokens, candid_in, candid_out,
                  silent_on_trap, my_principal);
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
    // '(variant { Ok = record { status_code = 200 : nat16; output = "Ready to start a new chat for cache file .canister_cache/expmt-gtxsw-inftj-ttabj-qhp5s-nozup-n3bbo-k7zvn-dg4he-knac3-lae/my_cache/prompt.cache"; input = ""; error=""; prompt_remaining="" } })'
    mockIC.run_test(
        "new_chat " + std::to_string(i) + " - " + model, new_chat,
        "4449444c026c01dd9ad28304016d710100020e2d2d70726f6d70742d6361636865156d795f63616368652f70726f6d70742e6361636865",
        "4449444c026c05819e846471c897a79907718a88f7f00b719aa1b2f90c7adb92a2c90d716b01bc8a01000101008e01526561647920746f2073746172742061206e6577206368617420666f722063616368652066696c65202e63616e69737465725f63616368652f6578706d742d67747873772d696e66746a2d747461626a2d71687035732d6e6f7a75702d6e3362626f2d6b377a766e2d64673468652d6b6e6163332d6c61652f6d795f63616368652f70726f6d70742e63616368650000c80000",
        silent_on_trap, my_principal);

    // -----------------------------------------------------------------------------
    // Feed the system prompt into the cache:
    // (NOTE: for long system prompts, this must be done in a loop)
    // -sp  : special token output enabled
    // -n 1 : let it generate 1 token.
    //        -> this is NOT stored in the cache, because the last token never is
    // '(record { args = vec {"--prompt-cache"; "my_cache/prompt.cache"; "--prompt-cache-all"; "-sp"; "-n"; "1"; "-p"; "<|im_start|>system\nYou are a helpful assistant.<|im_end|>\n"} })' ->
    // '(variant { Ok = record { status_code = 200 : nat16; output = "TODO" } })'
    mockIC.run_test(
        "run_update for chat " + std::to_string(i) + " - " + model, run_update,
        "4449444c026c01dd9ad28304016d710100080e2d2d70726f6d70742d6361636865156d795f63616368652f70726f6d70742e6361636865122d2d70726f6d70742d63616368652d616c6c032d7370022d6e0131022d703a3c7c696d5f73746172747c3e73797374656d0a596f752061726520612068656c7066756c20617373697374616e742e3c7c696d5f656e647c3e0a",
        "44444", silent_on_trap, my_principal);

    // -----------------------------------------------------------------------------
    // Feed the user prompt into the cache & indicate it is now the turn of the assistant:
    // (NOTE: for long user prompts, this must be done in a loop)
    // -sp  : special token output enabled
    // -n 1 : let it generate 1 token.
    //        -> this is NOT stored in the cache, because the last token never is
    // '(record { args = vec {"--prompt-cache"; "my_cache/prompt.cache"; "--prompt-cache-all"; "-sp"; "-n"; "1"; "-p"; "<|im_start|>user\nExplain Large Language Models.<|im_end|>\n<|im_start|>assistant\n"} })' ->
    // '(variant { Ok = record { status_code = 200 : nat16; output = "TODO" } })'
    mockIC.run_test(
        "run_update for chat " + std::to_string(i) + " - " + model, run_update,
        "4449444c026c01dd9ad28304016d710100080e2d2d70726f6d70742d6361636865156d795f63616368652f70726f6d70742e6361636865122d2d70726f6d70742d63616368652d616c6c032d7370022d6e0131022d70503c7c696d5f73746172747c3e757365720a4578706c61696e204c61726765204c616e6775616765204d6f64656c732e3c7c696d5f656e647c3e0a3c7c696d5f73746172747c3e617373697374616e740a",
        "44444", silent_on_trap, my_principal);

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
        "run_update for chat " + std::to_string(i) + " continued - " + model,
        run_update,
        "4449444c026c01dd9ad28304016d7101000d0e2d2d70726f6d70742d6361636865156d795f63616368652f70726f6d70742e6361636865122d2d70726f6d70742d63616368652d616c6c0a2d2d73616d706c65727305746f705f70062d2d74656d7003302e31072d2d746f702d7003302e39022d6e023230022d7000",
        "", silent_on_trap, my_principal);
  }
}