// Native (MockIC) smoke test for the default reference model, Qwen3-0.6B-Q8_0.
//
// Intent: verify the Qwen3 architecture LOADS on the native build, that the
// non-thinking chat pipeline (new_chat -> ingest -> generate) runs without
// trapping, and that it produces the EXACT expected tokens.
//
// The ingest/generate calls decode greedily (`--samplers temperature --temp
// 0.0`) precisely so their tokens CAN be pinned; under the default sampler a
// fresh random seed is drawn per run and nothing is assertable. This is the
// only exact-token generation guard on a Qwen-class model, so it is what
// catches silently wrong logits. The richer behavioral checks (no `<think>` in
// output, multi-turn recall, memory limit) live in the pytest
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
  //   "--cache-type-k"; "q8_0"; "--cache-type-v"; "q8_0"; "--batch-size"; "64";
  //   "--ubatch-size"; "64"; "--ctx-size"; "16384"} })'
  test_name = std::string(__func__) + ": " + "load_model - " + model;
  candid_in =
      "4449444c026c01dd9ad28304016d7101000c072d2d6d6f64656c306d6f64656c732f5177656e"
      "2f5177656e332d302e36422d474755462f5177656e332d302e36422d51385f302e676775660e"
      "2d2d63616368652d747970652d6b0471385f300e2d2d63616368652d747970652d760471385f"
      "300c2d2d62617463682d73697a650236340d2d2d7562617463682d73697a650236340a2d2d63"
      "74782d73697a65053136333834";
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
  // From here on the calls pin the EXACT generated tokens. That is only possible
  // because they pass `--samplers temperature --temp 0.0` (greedy): the default
  // sampler draws a fresh random seed on every run ("sampler seed: ..." in the
  // log), so its output can never be asserted. Greedy decoding is deterministic
  // and seed-independent.
  //
  // This is the only exact-token GENERATION guard on a Qwen model - the
  // TinyStories tests pin greedy tokens too, but on 260K/15M toy models. Without
  // this, a change that subtly corrupts the logits (wrong KV masking, a bad
  // n_kv/batch bound, a botched fork re-vendor) still passes every other test,
  // because every other Qwen generation assertion is a no-trap check only.
  //
  // Exact tokens are architecture-dependent, which is why the native job is
  // pinned to x86_64 in .github/workflows/cicd-mac.yml. Re-baseline these two
  // hexes (never hand-edit them) after any intentional llama.cpp upgrade.

  // ---------------------------------------------------------------------------
  // run_update: ingest the non-thinking prompt (assistant turn ends with an
  // empty `<think></think>` block), -n 1, greedy.
  test_name =
      std::string(__func__) + ": " + "run_update ingest greedy - " + model;
  candid_in =
      "4449444c026c01dd9ad28304016d710100100e2d2d70726f6d70742d63616368650c70726f6d70"
      "742e6361636865122d2d70726f6d70742d63616368652d616c6c0e2d2d63616368652d74797065"
      "2d6b0471385f300e2d2d63616368652d747970652d760471385f300a2d2d73616d706c6572730b"
      "74656d7065726174757265062d2d74656d7003302e30032d7370022d70673c7c696d5f73746172"
      "747c3e757365720a476976652061206f6e652073656e74656e636520696e74726f20746f204c4c"
      "4d732e3c7c696d5f656e647c3e0a3c7c696d5f73746172747c3e617373697374616e740a3c7468"
      "696e6b3e0a0a3c2f7468696e6b3e0a0a022d6e0131";
  // input="<|im_start|>user\nGive a one sentence intro to LLMs", output="",
  // prompt_remaining=".<|im_end|>\n<|im_start|>assistant\n<think>\n\n</think>\n\n"
  candid_out =
      "4449444c036c0b84d28e1701819e846471db92ea8f0501838fe5800671c897a7990771bbb1bbe2"
      "0801fde19a880c019aa1b2f90c7adb92a2c90d71cdd9e6b30e7efba3dbe30e016e786b01bc8a01"
      "0001020001160000000000000000010000000000000000323c7c696d5f73746172747c3e757365"
      "720a476976652061206f6e652073656e74656e636520696e74726f20746f204c4c4d7300010a00"
      "000000000000010000000000000000c800352e3c7c696d5f656e647c3e0a3c7c696d5f73746172"
      "747c3e617373697374616e740a3c7468696e6b3e0a0a3c2f7468696e6b3e0a0a00010c00000000"
      "000000";
  mockIC.run_test(test_name, run_update, candid_in, candid_out, silent_on_trap,
                  my_principal);

  // ---------------------------------------------------------------------------
  // run_update: generate (empty prompt, -n 20), greedy -> exact tokens pinned.
  test_name =
      std::string(__func__) + ": " + "run_update generate greedy - " + model;
  candid_in =
      "4449444c026c01dd9ad28304016d710100100e2d2d70726f6d70742d63616368650c70726f6d70"
      "742e6361636865122d2d70726f6d70742d63616368652d616c6c0e2d2d63616368652d74797065"
      "2d6b0471385f300e2d2d63616368652d747970652d760471385f300a2d2d73616d706c6572730b"
      "74656d7065726174757265062d2d74656d7003302e30032d7370022d7000022d6e023230";
  // THE exact-token assertion: 12 greedy tokens through the real decode path
  // (q8_0 KV, ctx 16384). output="\ninspired by the example given\ninspired by the"
  candid_out =
      "4449444c036c0b84d28e1701819e846471db92ea8f0501838fe5800671c897a7990771bbb1bbe2"
      "0801fde19a880c019aa1b2f90c7adb92a2c90d71cdd9e6b30e7efba3dbe30e016e786b01bc8a01"
      "00010200010c000000000000002e0a696e73706972656420627920746865206578616d706c6520"
      "676976656e0a696e73706972656420627920746865010c00000000000000603c7c696d5f737461"
      "72747c3e757365720a476976652061206f6e652073656e74656e636520696e74726f20746f204c"
      "4c4d730a696e73706972656420627920746865206578616d706c6520676976656e0a696e737069"
      "7265642062792074686500010000000000000000010c00000000000000c8000000010000000000"
      "000000";
  mockIC.run_test(test_name, run_update, candid_in, candid_out, silent_on_trap,
                  my_principal);
}
