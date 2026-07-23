// Native (MockIC) smoke test for the default reference model, Qwen3-0.6B-Q8_0.
//
// Intent: verify the Qwen3 architecture LOADS on the native build and that the
// non-thinking chat pipeline (new_chat -> ingest -> generate) runs without
// trapping. Native runs on the host (no wasm memory limit and a different
// tokenizer/float path than the deployed wasm), so we deliberately DO NOT pin
// exact generated tokens — the generation calls use an empty expected output
// (MockIC then only requires "did not trap"). The richer behavioral checks
// (no `<think>` in output, multi-turn recall, memory limit) live in the pytest
// test/test_qwen3.py which runs against a real local replica.
//
// The model is read from disk at the path below; CI (.github/workflows/
// cicd-mac.yml) downloads it before running the native tests.

#include "test_qwen3.h"

#include "../src/max_tokens.h"
#include "../src/model.h"
#include "../src/run.h"

#include "mock_ic.h"

#include <iostream>
#include <string>

void test_qwen3(MockIC &mockIC) {
  std::string my_principal{MOCKIC_CONTROLLER};
  bool silent_on_trap = true;

  std::string model = "models/Qwen/Qwen3-0.6B-GGUF/Qwen3-0.6B-Q8_0.gguf";
  std::cout << "\n========== test_qwen3 ==========\n";
  std::cout << "Loading & inferencing model: " << model << std::endl;

  std::string test_name;
  std::string candid_in;
  std::string candid_out;

  // ---------------------------------------------------------------------------
  // load_model with the tuned Qwen3 config (memory-marginal in wasm; on native
  // there is no wasm memory limit so it loads regardless):
  // '(record { args = vec {"--model"; "models/Qwen/Qwen3-0.6B-GGUF/Qwen3-0.6B-Q8_0.gguf";
  //   "--cache-type-k"; "q8_0"; "--cache-type-v"; "q8_0"; "--ctx-size"; "1024"} })'
  test_name = std::string(__func__) + ": " + "load_model - " + model;
  candid_in =
      "4449444c026c01dd9ad28304016d71010008072d2d6d6f64656c306d6f64656c732f5177656e2f"
      "5177656e332d302e36422d474755462f5177656e332d302e36422d51385f302e676775660e2d2d"
      "63616368652d747970652d6b0471385f300e2d2d63616368652d747970652d760471385f300a2d"
      "2d6374782d73697a650431303234";
  // Load-success OutputRecord is model-independent:
  // '(variant { Ok = record { status_code = 200; input=""; prompt_remaining="";
  //   output="Model succesfully loaded into memory."; error=""; generated_eog=false } })'
  candid_out =
      "4449444c026c06819e846471838fe5800671c897a79907719aa1b2f90c7adb92a2c90d71cdd9e6"
      "b30e7e6b01bc8a0100010100254d6f64656c2073756363657366756c6c79206c6f6164656420696"
      "e746f206d656d6f72792e0000c8000000";
  mockIC.run_test(test_name, load_model, candid_in, candid_out, silent_on_trap,
                  my_principal);

  // ---------------------------------------------------------------------------
  // set_max_tokens (query=12, update=12) — bound per-call generation.
  test_name = std::string(__func__) + ": " + "set_max_tokens - " + model;
  candid_in =
      "4449444c016c02deb5daad0478f3a29d8e077801000c000000000000000c00000000000000";
  candid_out = "4449444c026c019aa1b2f90c7a6b01bc8a0100010100c800";
  mockIC.run_test(test_name, set_max_tokens, candid_in, candid_out,
                  silent_on_trap, my_principal);

  // ---------------------------------------------------------------------------
  // new_chat:
  // '(record { args = vec {"--prompt-cache"; "prompt.cache"; "--cache-type-k"; "q8_0";
  //   "--cache-type-v"; "q8_0"} })' -> just require no trap.
  test_name = std::string(__func__) + ": " + "new_chat - " + model;
  candid_in =
      "4449444c026c01dd9ad28304016d710100060e2d2d70726f6d70742d63616368650c70726f6d70"
      "742e63616368650e2d2d63616368652d747970652d6b0471385f300e2d2d63616368652d747970"
      "652d760471385f30";
  candid_out = "";
  mockIC.run_test(test_name, new_chat, candid_in, candid_out, silent_on_trap,
                  my_principal);

  // ---------------------------------------------------------------------------
  // run_update: ingest the non-thinking prompt (assistant turn ends with an
  // empty `<think></think>` block), -n 1. Tokens are non-deterministic ->
  // empty expected (no-trap check only).
  test_name = std::string(__func__) + ": " + "run_update ingest - " + model;
  candid_in =
      "4449444c026c01dd9ad28304016d7101000c0e2d2d70726f6d70742d63616368650c70726f6d70"
      "742e6361636865122d2d70726f6d70742d63616368652d616c6c0e2d2d63616368652d74797065"
      "2d6b0471385f300e2d2d63616368652d747970652d760471385f30032d7370022d70673c7c696d"
      "5f73746172747c3e757365720a476976652061206f6e652073656e74656e636520696e74726f20"
      "746f204c4c4d732e3c7c696d5f656e647c3e0a3c7c696d5f73746172747c3e617373697374616e"
      "740a3c7468696e6b3e0a0a3c2f7468696e6b3e0a0a022d6e0131";
  candid_out = "";
  mockIC.run_test(test_name, run_update, candid_in, candid_out, silent_on_trap,
                  my_principal);

  // ---------------------------------------------------------------------------
  // run_update: generate (empty prompt, -n 20) -> require no trap.
  test_name = std::string(__func__) + ": " + "run_update generate - " + model;
  candid_in =
      "4449444c026c01dd9ad28304016d7101000c0e2d2d70726f6d70742d63616368650c70726f6d70"
      "742e6361636865122d2d70726f6d70742d63616368652d616c6c0e2d2d63616368652d74797065"
      "2d6b0471385f300e2d2d63616368652d747970652d760471385f30032d7370022d7000022d6e02"
      "3230";
  candid_out = "";
  mockIC.run_test(test_name, run_update, candid_in, candid_out, silent_on_trap,
                  my_principal);
}
