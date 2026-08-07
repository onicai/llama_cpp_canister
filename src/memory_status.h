// Reports the canister's current memory usage.
//
// Two cheap, in-canister introspection values (no inter-canister call):
//   - wasm_heap_bytes : the wasm linear-memory high-water-mark
//     (__builtin_wasm_memory_size(0) * 64 KiB). This is the number that climbs
//     toward the canister's wasm_memory_limit and, when it reaches it, causes
//     "heap out of bounds" (IC0502) traps during model load / decode. Useful to
//     watch when running larger models (e.g. Qwen3-0.6B) close to the limit.
//   - stable_bytes : stable-memory size (ic0.stable64_size() * 64 KiB), which
//     holds the uploaded model file and the virtual filesystem (prompt caches,
//     logs) via ic-wasi-polyfill. The 64-bit API is required: the 32-bit
//     ic0.stable_size traps once stable memory passes 4 GiB, which happens as
//     soon as a canister holds more than one large gguf.
//
// Access: non-anonymous callers only (anonymous -> access denied).
#pragma once

#include "wasm_symbol.h"

// Query endpoint — RBAC: rejects anonymous callers, any other principal allowed.
void get_memory_status()
    WASM_SYMBOL_EXPORTED("canister_query get_memory_status");
