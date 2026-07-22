// WASI C++ exception ABI stubs.
//
// llama.cpp uses throw/catch, but WASI has no unwind runtime. Compiling with
// -fexceptions plus these stubs lets upstream sources build unmodified, which
// replaces the per-file `throw` -> IC_API::trap rewriting earlier upgrades did.
//
// Behavior: a throw TRAPS the canister with a diagnostic message, preserving
// the observable semantics llama_cpp_canister has always had. The message is
// propagated to the caller instead of being lost to a bare abort().
//
// IMPORTANT: there is no unwinding, so `catch` blocks never run. Any upstream
// try/catch that used to RECOVER now traps instead. Code paths that throw as
// part of normal operation must be guarded with #ifdef __wasi__. Known guards:
//   - params.warmup = false in common/common.cpp (throws during llama_decode)
//   - "--model is required" in common/arg.cpp returns false instead of throwing
//   - getenv() call sites (garbage pointers under the polyfill)
//
// Adapted from instruction-bounded-inference-artifact
// MIT (c) 2026 Julien Aerni, Simeon Fluck, Dustin Becker
// Modified for llama_cpp_canister: traps via IC_API::trap rather than abort().

// Native (build-native / MockIC) builds must NOT see this file: it redefines
// the C++ exception ABI, which libc++ provides for real on the host.
#ifdef __wasi__

#include <cstdlib>
#include <cstring>
#include <exception>
#include <string>
#include <typeinfo>

#include "ic_api.h"

namespace {

// Reading what() requires dereferencing the thrown object through a vtable.
// That is only safe if the object really is a std::exception, so gate it on
// the Itanium-ABI mangled name: std:: types mangle with a leading "St".
bool looks_like_std_exception(const std::type_info *tinfo) {
  if (tinfo == nullptr) return false;
  const char *n = tinfo->name();
  return n != nullptr && n[0] == 'S' && n[1] == 't';
}

} // namespace

extern "C" {

void *__cxa_allocate_exception(unsigned long size) {
  void *ptr = std::malloc(size);
  if (ptr) std::memset(ptr, 0, size);
  return ptr;
}

void __cxa_free_exception(void *ptr) { std::free(ptr); }

__attribute__((noreturn)) void
__cxa_throw(void *thrown_object, std::type_info *tinfo, void (*dest)(void *)) {
  std::string msg = "UNCAUGHT C++ EXCEPTION";

  if (tinfo != nullptr && tinfo->name() != nullptr) {
    msg += " [";
    msg += tinfo->name();
    msg += "]";
  }

  // Only touch the object when the type says it is safe to.
  if (thrown_object != nullptr && looks_like_std_exception(tinfo)) {
    const char *what = static_cast<std::exception *>(thrown_object)->what();
    if (what != nullptr) {
      msg += ": ";
      msg += what;
    }
  }

  IC_API::trap(msg); // [[noreturn]] — satisfies this function's contract
}

// No unwinder, so these are unreachable in practice. They exist so that
// upstream try/catch blocks still link.
void *__cxa_begin_catch(void *exc_obj) { return exc_obj; }
void __cxa_end_catch() {}

__attribute__((noreturn)) void __cxa_rethrow() {
  IC_API::trap(
      "UNCAUGHT C++ EXCEPTION: __cxa_rethrow with no active exception");
}

} // extern "C"

#endif // __wasi__
