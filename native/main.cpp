// Main entry point for a native debug executable.
// Build it with: `icpp build-native` from the parent folder where 'icpp.toml' resides

#include "main.h"
#include "test_canister_functions.h"
#include "test_qwen2.h"
#include "test_tiny_stories.h"

#include <iostream>

#include "../src/auth.h"
#include "../src/health.h"
#include "../src/logs.h"
#include "../src/model.h"
#include "../src/ready.h"
#include "../src/run.h"
#include "../src/upload.h"
#include "../src/whoami.h"

// The Mock IC
#include "mock_ic.h"

int main() {
  bool exit_on_fail = true;
  MockIC mockIC(exit_on_fail);

  test_canister_functions(mockIC);
  test_tiny_stories(mockIC);
  test_qwen2(mockIC);

  // returns 1 if any tests failed
  return mockIC.test_summary();
}