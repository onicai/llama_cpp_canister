// Native tests for the memory-status query endpoint.
//
// get_memory_status reports wasm heap + stable memory sizes. Those byte values
// are runtime/host-dependent (non-deterministic), so the Ok case uses an empty
// expected output ("") — MockIC then only requires that the call does not trap.
// The anonymous case must return the shared "Access Denied" ApiError.

#include "test_memory_status.h"

#include "../src/memory_status.h"

#include "ic_api.h"
#include "mock_ic.h"

#include <iostream>
#include <string>

void test_memory_status(MockIC &mockIC) {
  std::string controller_principal{MOCKIC_CONTROLLER};
  std::string anonymous_principal{"2vxsx-fae"};
  bool silent_on_trap = true;

  // didc encode '()'
  const std::string EMPTY_INPUT = "4449444c0000";
  // ACTUAL C++ output of send_access_denied_api_error (same hardcoded value
  // used by test_cycle_balance / test_cache_cleanup).
  const std::string ACCESS_DENIED_API_ERROR =
      "4449444c026b01b0ad8fcd0c716b01c5fed20100010100000d4163636573732044656e696"
      "564";

  std::cout << "\n========== test_memory_status ==========\n";

  // Anonymous callers are denied.
  mockIC.run_test("get_memory_status (anon denied)", get_memory_status,
                  EMPTY_INPUT, ACCESS_DENIED_API_ERROR, silent_on_trap,
                  anonymous_principal);

  // A non-anonymous caller (controller) succeeds. The reported byte values are
  // non-deterministic, so we only require that the call does not trap (empty
  // expected output).
  mockIC.run_test("get_memory_status (non-anon ok)", get_memory_status,
                  EMPTY_INPUT, "", silent_on_trap, controller_principal);
}
