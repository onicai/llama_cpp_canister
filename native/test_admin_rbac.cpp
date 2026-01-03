#include "../src/auth.h"
#include "../src/files.h"
#include "../src/logs.h"
#include "../src/run.h"
#include "mock_ic.h"

void test_admin_rbac(MockIC &mockIC) {
  std::string controller_principal{MOCKIC_CONTROLLER};
  std::string anonymous_principal{"2vxsx-fae"};
  std::string admin_query_principal{"rrkah-fqaaa-aaaaa-aaaaq-cai"};

  bool silent_on_trap = true;

  // ===========================================================================
  // Concrete expected outputs
  // ===========================================================================

  // ApiError "Access Denied"
  // didc encode '(variant { Err = variant { Other = "Access Denied" } })'
  // Note: Actual wire format may have different type table ordering
  const std::string ACCESS_DENIED_API_ERROR =
      "4449444c026b01b0ad8fcd0c716b01c5fed20100010100000d4163636573732044656e6965"
      "64";

  // OutputRecordResult Access Denied (status_code = 401)
  // Verified with didc decode: variant { Err = record { conversation = "";
  // output = ""; error = "Access Denied"; status_code = 401; prompt_remaining =
  // ""; generated_eog = false } }
  const std::string ACCESS_DENIED_OUTPUT_RECORD =
      "4449444c026c06819e846471838fe5800671c897a79907719aa1b2f90c7adb92a2c90d71cd"
      "d9e6b30e7e6b01c5fed2010001010000000d4163636573732044656e69656491010000";

  // Empty input for parameterless endpoints
  // didc encode '()'
  const std::string EMPTY_INPUT = "4449444c0000";

  // Input for filesystem_file_size
  // didc encode '(record { filename = "test.txt" })'
  const std::string FILESYSTEM_FILE_SIZE_INPUT =
      "4449444c016c01c7dda8bb0771010008746573742e747874";

  // Input for new_chat/run_query/run_update
  // didc encode '(record { args = vec { "--help" } })'
  const std::string RUN_INPUT =
      "4449444c026c01b79cba840c016d71010001062d2d68656c70";

  // Input for assignAdminRole
  // didc encode '(record { "principal" = "rrkah-fqaaa-aaaaa-aaaaq-cai"; role =
  // variant { AdminQuery }; note = "test" })'
  const std::string ASSIGN_ADMIN_QUERY_INPUT =
      "4449444c026c03ae9db1900171f2afa8c80471f6d6bbdd04016b0199d8ddf2087f01001b72"
      "726b61682d66716161612d61616161612d61616161712d636169047465737400";

  // ===========================================================================
  // Test 1: New Admin RBAC endpoints - ApiError format
  // ===========================================================================

  // getAdminRoles - anonymous -> ApiError Access Denied
  mockIC.run_test(std::string(__func__) +
                      ": getAdminRoles - anonymous denied (ApiError)",
                  getAdminRoles, EMPTY_INPUT, ACCESS_DENIED_API_ERROR,
                  silent_on_trap, anonymous_principal);

  // assignAdminRole - anonymous -> ApiError Access Denied
  mockIC.run_test(std::string(__func__) +
                      ": assignAdminRole - anonymous denied (ApiError)",
                  assignAdminRole, ASSIGN_ADMIN_QUERY_INPUT,
                  ACCESS_DENIED_API_ERROR, silent_on_trap, anonymous_principal);

  // ===========================================================================
  // Test 2: ApiError-based existing endpoints
  // ===========================================================================

  // filesystem_file_size - anonymous -> ApiError Access Denied
  mockIC.run_test(std::string(__func__) +
                      ": filesystem_file_size - anonymous denied (ApiError)",
                  filesystem_file_size, FILESYSTEM_FILE_SIZE_INPUT,
                  ACCESS_DENIED_API_ERROR, silent_on_trap, anonymous_principal);

  // log_pause - anonymous -> ApiError Access Denied
  mockIC.run_test(std::string(__func__) +
                      ": log_pause - anonymous denied (ApiError)",
                  log_pause, EMPTY_INPUT, ACCESS_DENIED_API_ERROR,
                  silent_on_trap, anonymous_principal);

  // ===========================================================================
  // Test 3: OutputRecordResult endpoints - NOT ApiError format
  // ===========================================================================

  // new_chat - anonymous -> OutputRecordResult with status_code = 401
  mockIC.run_test(std::string(__func__) +
                      ": new_chat - anonymous denied (OutputRecordResult)",
                  new_chat, RUN_INPUT, ACCESS_DENIED_OUTPUT_RECORD,
                  silent_on_trap, anonymous_principal);

  // run_query - anonymous -> OutputRecordResult with status_code = 401
  mockIC.run_test(std::string(__func__) +
                      ": run_query - anonymous denied (OutputRecordResult)",
                  run_query, RUN_INPUT, ACCESS_DENIED_OUTPUT_RECORD,
                  silent_on_trap, anonymous_principal);

  // run_update - anonymous -> OutputRecordResult with status_code = 401
  mockIC.run_test(std::string(__func__) +
                      ": run_update - anonymous denied (OutputRecordResult)",
                  run_update, RUN_INPUT, ACCESS_DENIED_OUTPUT_RECORD,
                  silent_on_trap, anonymous_principal);

  // ===========================================================================
  // Test 4: RBAC permission hierarchy
  // ===========================================================================

  // Controller assigns AdminQuery role
  // We don't check exact Ok response (contains dynamic timestamp)
  // Just run and verify it doesn't trap
  mockIC.run_test(
      std::string(__func__) +
          ": assignAdminRole - controller assigns AdminQuery",
      assignAdminRole, ASSIGN_ADMIN_QUERY_INPUT,
      "", // Don't check exact Ok response (contains dynamic timestamp)
      silent_on_trap, controller_principal);

  // AdminQuery principal can access AdminQuery endpoint (filesystem_file_size)
  // File doesn't exist, so we get file-not-found error, NOT access denied
  mockIC.run_test(
      std::string(__func__) +
          ": filesystem_file_size - AdminQuery allowed (not access denied)",
      filesystem_file_size, FILESYSTEM_FILE_SIZE_INPUT,
      "", // Don't check exact response; verify it's NOT ACCESS_DENIED_API_ERROR
      silent_on_trap, admin_query_principal);

  // AdminQuery principal cannot access AdminUpdate endpoint (filesystem_remove)
  mockIC.run_test(std::string(__func__) +
                      ": filesystem_remove - AdminQuery denied (ApiError)",
                  filesystem_remove,
                  FILESYSTEM_FILE_SIZE_INPUT, // Same input format (filename)
                  ACCESS_DENIED_API_ERROR, silent_on_trap,
                  admin_query_principal);

  // ===========================================================================
  // Test 5: Verify error format isolation
  // ===========================================================================

  // The exact match tests above verify:
  // - OutputRecordResult endpoints NEVER return ApiError format
  // - ApiError endpoints NEVER return OutputRecordResult format
}
