// Memory-status query endpoint — implementation.
// See memory_status.h for the high-level contract.

#include "memory_status.h"

#include "auth.h"
#include "ic0.h"
#include "ic_api.h"

#include <cstdint>
#include <string>

void get_memory_status() {
  IC_API ic_api(CanisterQuery{std::string(__func__)}, false);

  // Access: reject anonymous callers; any authenticated principal may read.
  if (ic_api.get_caller().is_anonymous()) {
    send_access_denied_api_error(ic_api);
    return;
  }
  ic_api.from_wire();

#ifdef __wasi__
  // Wasm linear-memory pages -> bytes (each page is 64 KiB). This is the heap
  // high-water-mark that approaches the wasm_memory_limit and trips IC0502.
  const uint64_t wasm_heap_bytes =
      static_cast<uint64_t>(__builtin_wasm_memory_size(0)) * 65536ULL;
  // Stable-memory pages -> bytes (uploaded model file + virtual filesystem).
  const uint64_t stable_bytes = static_cast<uint64_t>(stable_size()) * 65536ULL;
#else
  // Native (MockIC) build: no wasm linear memory / IC stable memory. The
  // wasm-memory builtin does not exist off-target, so report 0 here; the real
  // values are only meaningful in the deployed wasm canister.
  const uint64_t wasm_heap_bytes = 0;
  const uint64_t stable_bytes = 0;
#endif

  CandidTypeRecord r;
  r.append("wasm_heap_bytes", CandidTypeNat64{wasm_heap_bytes});
  r.append("stable_bytes", CandidTypeNat64{stable_bytes});
  ic_api.to_wire(CandidTypeVariant{"Ok", CandidTypeRecord{r}});
}
