#pragma once
#include "mock_ic.h"
#include <cstdint>
#include <string>

// The status_code of an OutputRecordResult reply.
uint16_t status_of(const std::string &hex);

void test_run_update_full(MockIC &mockIC);
