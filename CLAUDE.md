# Claude Instructions for llama_cpp_canister

## Project Overview

This is a C++ canister for running llama.cpp on the Internet Computer (ICP), built with icpp-pro.

## Filesystem Architecture

**Important:** ICP canisters do NOT have access to a traditional host filesystem. Instead, icpp-pro provides a virtual filesystem backed by stable memory through:

- [ic-wasi-polyfill](https://github.com/wasm-forge/ic-wasi-polyfill)
- [wasi2ic](https://github.com/wasm-forge/wasi2ic)

**Key characteristics:**
- Standard file operations (`fopen`, `std::ifstream`, etc.) work on mainnet
- Files persist across canister upgrades (stored in stable memory)
- No access to host OS filesystem (`/etc/passwd`, etc. don't exist)
- The virtual filesystem is contained entirely within the canister's stable memory

**Security implications:**
- Path traversal attacks cannot escape to the host system
- File operations are sandboxed within the canister's virtual filesystem
- Controllers already have full access to all canister data, so path traversal by controllers is not a privilege escalation

## Build Commands

```bash
# Activate conda environment
source /opt/miniconda3/etc/profile.d/conda.sh && conda activate llama_cpp_canister

# Build native executable (for unit tests)
icpp build-native

# Build WASM (for deployment)
icpp build-wasm

# Deploy to local network
dfx start --background
dfx deploy --network local

# Deploy to IC mainnet
dfx deploy --network ic
```

## Testing

### Unit Tests (Native MockIC)

Location: `native/test_*.cpp`

**Pattern:**
```cpp
#include "../src/your_module.h"
#include "mock_ic.h"

void test_your_module(MockIC &mockIC) {
    std::string my_principal{MOCKIC_CONTROLLER};
    bool silent_on_trap = true;

    // Test format: name, function, input_hex, expected_output_hex, silent, principal
    mockIC.run_test(
        std::string(__func__) + ": " + "test description",
        your_canister_function,
        "4449444c...",  // Candid-encoded input (hex)
        "4449444c...",  // Candid-encoded expected output (hex)
        silent_on_trap,
        my_principal
    );
}
```

**Generating Candid hex strings:**
```bash
# Encode input
didc encode '(record { filename = "test.bin"; chunksize = 1024 : nat64; offset = 0 : nat64 })'

# Decode output (for debugging)
didc decode 4449444c...
```

**Running unit tests:**
```bash
icpp build-native && ./build-native/mockic.exe
```

**Adding a new test file:**
1. Create `native/test_yourmodule.cpp` and `native/test_yourmodule.h`
2. Add include to `native/main.cpp`
3. Call your test function from `main()`

### Smoke Tests (Python pytest)

Location: `test/test_*.py`

**Pattern:**
```python
from pathlib import Path
from icpp.smoketest import call_canister_api

DFX_JSON_PATH = Path(__file__).parent / "../dfx.json"
CANISTER_NAME = "llama_cpp"

def test__your_test_name(network: str, principal: str) -> None:
    response = call_canister_api(
        dfx_json_path=DFX_JSON_PATH,
        canister_name=CANISTER_NAME,
        canister_method="your_canister_method",
        canister_argument='(record { field = "value" })',
        network=network,
    )
    expected_response = '(variant { Ok = ... })'
    assert response == expected_response
```

**Running smoke tests:**
```bash
# Deploy first
dfx start --clean --background
dfx deploy --network local

# Run specific test
pytest -vv --network local test/test_files.py::test__your_test_name

# Run all tests in a file
pytest -vv --network local test/test_files.py
```

**Available fixtures (from conftest.py):**
- `network` - "local" or "ic"
- `principal` - Current identity's principal
- `identity_anonymous` - Dict with anonymous identity info

## Code Patterns

### Shared Constants

Define in `src/utils.h` for reuse across modules:
```cpp
// Security limits to prevent unbounded memory allocation from user input
const uint64_t MAX_CHUNK_SIZE = 2 * 1024 * 1024; // ICP message size limit
const uint64_t MAX_FILENAME_SIZE = 4096;         // Linux PATH_MAX
const uint64_t MAX_SHA256_SIZE = 64;             // SHA256 hex string length
```

### Input Validation

Always validate user input before allocation or use:
```cpp
if (chunksize > MAX_CHUNK_SIZE) {
    ic_api.to_wire(CandidTypeVariant{
        "Err", CandidTypeVariant{
                   "Other", CandidTypeText{
                                std::string(__func__) + ": chunksize " +
                                std::to_string(chunksize) + " exceeds limit " +
                                std::to_string(MAX_CHUNK_SIZE)}}});
    return;
}
```

### Error Response Format

Use nested variants for errors:
```cpp
ic_api.to_wire(CandidTypeVariant{
    "Err", CandidTypeVariant{"Other", CandidTypeText{"Error message"}}});
```

### Controller Check

Most sensitive endpoints require controller access:
```cpp
void your_function() {
    IC_API ic_api(CanisterQuery{std::string(__func__)}, false);
    if (!is_caller_a_controller(ic_api)) return;
    // ... rest of function
}
```

## File Structure

```
llama_cpp_canister/
├── src/                    # Source files
│   ├── *.cpp / *.h        # Canister implementation
│   └── utils.h            # Shared constants and utilities
├── native/                 # Native unit tests
│   ├── main.cpp           # Test runner entry point
│   ├── test_*.cpp/h       # Test files
│   └── mock_ic.h          # MockIC framework (from icpp)
├── test/                   # Python smoke tests
│   ├── conftest.py        # pytest fixtures
│   └── test_*.py          # Smoke test files
├── build-native/           # Native build output
├── build/                  # WASM build output
└── dfx.json               # DFX configuration
```

## Workflow for Adding Security Fixes

1. **Add constants** to `src/utils.h` if needed
2. **Implement validation** in the relevant `.cpp` file
3. **Add unit test** to appropriate `native/test_*.cpp`
4. **Build and run unit tests**: `icpp build-native && ./build-native/mockic.exe`
5. **Add smoke test** to appropriate `test/test_*.py`
6. **Deploy and run smoke tests**: `dfx deploy --network local && pytest -vv --network local test/test_*.py`

## Tips

- MockIC hex strings can differ in field ordering - run test first to get actual output
- `didc` tool is essential for encoding/decoding Candid
- Smoke tests depend on canister state - order of tests matters