#include <iostream>

#include "../src/auth.h"
#include "../src/health.h"
#include "../src/logs.h"
#include "../src/model.h"
#include "../src/prompt_cache.h"
#include "../src/ready.h"
#include "../src/run.h"
#include "../src/upload.h"
#include "../src/whoami.h"

// The Mock IC
#include "mock_ic.h"

void test_canister_functions(MockIC &mockIC) {

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
  mockIC.run_test(std::string(__func__) + ": " + "health", health,
                  "4449444c0000",
                  "4449444c026c019aa1b2f90c7a6b01bc8a0100010100c800",
                  silent_on_trap, anonymous_principal);

  // '()' -> '(variant { Err = record { Other = "Model not yet loaded"} })'
  mockIC.run_test(
      std::string(__func__) + ": " + "ready", ready, "4449444c0000",
      "4449444c026b01b0ad8fcd0c716b01c5fed2010001010000144d6f64656c206e6f7420796574206c6f61646564",
      silent_on_trap, anonymous_principal);

  // -----------------------------------------------------------------------------

  // '()' -> '("expmt-gtxsw-inftj-ttabj-qhp5s-nozup-n3bbo-k7zvn-dg4he-knac3-lae")'
  mockIC.run_test(
      std::string(__func__) + ": " + "whoami", whoami, "4449444c0000",
      "4449444c0001713f6578706d742d67747873772d696e66746a2d747461626a2d71687035732d6e6f7a75702d6e3362626f2d6b377a766e2d64673468652d6b6e6163332d6c6165",
      silent_on_trap, my_principal);

  // -----------------------------------------------------------------------------
  // Authentication (Whitelisting) functions

  // Only controller can call set_access
  // '(record { level = 0 : nat16 })' ->  '(variant { Err = variant { Other = "Access Denied" } })'
  mockIC.run_test(
      std::string(__func__) + ": " + "set_access Err", set_access,
      "4449444c016c0184ab8c93077a01000000",
      "4449444c026b01b0ad8fcd0c716b01c5fed20100010100000d4163636573732044656e696564",
      silent_on_trap, anonymous_principal);

  // Only controller can call get_access
  // '()' ->  '(variant { Err = variant { Other = "Access Denied" } })'
  mockIC.run_test(
      std::string(__func__) + ": " + "get_access Err", get_access,
      "4449444c0000",
      "4449444c026b01b0ad8fcd0c716b01c5fed20100010100000d4163636573732044656e696564",
      silent_on_trap, anonymous_principal);

  // '(record { level = 0 : nat16 })' ->
  // '(variant { Ok = record { level = 0 : nat16; explanation = "Only controllers"; } } )'
  mockIC.run_test(
      std::string(__func__) + ": " + "set_access to 0", set_access,
      "4449444c016c0184ab8c93077a01000000",
      "4449444c026c02d9b2b5ca047184ab8c93077a6b01bc8a0100010100104f6e6c7920636f6e74726f6c6c6572730000",
      silent_on_trap, my_principal);

  // '()' ->
  // '(variant { Ok = record { level = 0 : nat16; explanation = "Only controllers"; } } )'
  mockIC.run_test(
      std::string(__func__) + ": " + "get_access 0", get_access, "4449444c0000",
      "4449444c026c02d9b2b5ca047184ab8c93077a6b01bc8a0100010100104f6e6c7920636f6e74726f6c6c6572730000",
      silent_on_trap, my_principal);

  // '(record { level = 1 : nat16 })' ->
  // '(variant { Ok = record { level = 1 : nat16; explanation = "All except anonymous"; } } )'
  mockIC.run_test(
      std::string(__func__) + ": " + "set_access to 1", set_access,
      "4449444c016c0184ab8c93077a01000100",
      "4449444c026c02d9b2b5ca047184ab8c93077a6b01bc8a010001010014416c6c2065786365707420616e6f6e796d6f75730100",
      silent_on_trap, my_principal);

  // '()' ->
  // '(variant { Ok = record { level = 1 : nat16; explanation = "All except anonymous"; } } )'
  mockIC.run_test(
      std::string(__func__) + ": " + "get_access 1", get_access, "4449444c0000",
      "4449444c026c02d9b2b5ca047184ab8c93077a6b01bc8a010001010014416c6c2065786365707420616e6f6e796d6f75730100",
      silent_on_trap, my_principal);
}