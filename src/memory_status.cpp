// Memory-status query endpoint — implementation.
// See memory_status.h for the high-level contract.

#include "memory_status.h"

#include "auth.h"
#include "ic0.h"
#include "ic_api.h"

#include <cstdint>
#include <string>

#ifdef __wasi__
// icpp-pro's ic0.h only declares the 32-bit `stable_size`, which TRAPS with
// IC0502 ("32 bit stable memory api used on a memory larger than 4GB") once the
// canister's stable memory passes 4 GiB — which any canister holding more than
// one large gguf will do. Declare the 64-bit variant ourselves; it is a plain
// ic0 system-API import, so it survives wasi2ic untouched.
// See: https://internetcomputer.org/docs/current/references/ic-interface-spec#system-api-imports
// NOTE: the trailing `;` is redundant (the macro supplies one) but required in
// practice — without a visible semicolon clang-format cannot see where this
// declaration ends and re-indents whatever follows. That is exactly how
// icpp-pro's own ic0.h ended up merging `call_perform` and `stable_size` into
// one broken declaration. ic0.h appends it too; follow that convention.
extern "C" uint64_t ic0_stable64_size()
    WASM_SYMBOL_IMPORTED("ic0", "stable64_size");
#endif

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
  // Must be the 64-bit API: see the ic0_stable64_size note above.
  const uint64_t stable_bytes = ic0_stable64_size() * 65536ULL;
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
