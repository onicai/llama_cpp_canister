// WASI stubs for dynamic loading.
//
// ICP canisters cannot dlopen. ggml registers the CPU backend at compile time
// (-DGGML_USE_CPU), so ggml-backend-reg.cpp / ggml-backend-dl.cpp never need a
// real loader — they just need these symbols to link.
//
// dlopen returning nullptr makes ggml take its "no dynamic backend" path
// naturally, which is why this is a stub rather than a trap.
//
// Adapted from instruction-bounded-inference-artifact
// MIT (c) 2026 Julien Aerni, Simeon Fluck, Dustin Becker

// Native (build-native / MockIC) builds must NOT see this file: it redefines
// dlopen/dlsym/dlclose/dlerror, which libSystem provides for real on the host.
#ifdef __wasi__

extern "C" {

void *dlopen(const char *, int) { return nullptr; }

void *dlsym(void *, const char *) { return nullptr; }

int dlclose(void *) { return 0; }

char *dlerror(void) {
  static char msg[] = "dlopen not supported on WASI";
  return msg;
}

} // extern "C"

#endif // __wasi__
