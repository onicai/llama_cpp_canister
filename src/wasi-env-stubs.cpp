// WASI environment-variable stubs.
//
// A canister has no environment. Under the ic-wasi-polyfill the libc `environ`
// pointer is not set up the way wasi-libc's getenv expects, so calling the real
// getenv dereferences a garbage pointer and traps ("heap out of bounds",
// IC0502) at wasm address 0xffffffff -- observed via tty_can_use_colors() ->
// getenv() inside common_params_parse during load_model.
//
// Overriding getenv here (a strong symbol in an always-linked TU) satisfies the
// reference so the linker never pulls wasi-libc's getenv.o, and every getenv
// call site across llama.cpp/ggml safely receives nullptr. This is why the
// per-site #ifdef guards are NOT needed -- one override covers them all.
//
// (The reference fork guarded getenv call sites individually for the same
// reason: "getenv() calls guarded to prevent garbage pointer dereference".)

// Native (MockIC) builds must use the real libc getenv.
#ifdef __wasi__

#include <cstddef>

extern "C" {

char *getenv(const char *) { return nullptr; }

char *secure_getenv(const char *) { return nullptr; }

int setenv(const char *, const char *, int) { return 0; }

int unsetenv(const char *) { return 0; }

int putenv(char *) { return 0; }

} // extern "C"

#endif // __wasi__
