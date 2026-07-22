// WASI shim: <__mutex/lock_guard.h> — single-threaded no-op on WASI, transparent on native
//
// Adapted from instruction-bounded-inference-artifact
// MIT (c) 2026 Julien Aerni, Simeon Fluck, Dustin Becker
#ifndef __wasi__
// Native: defer entirely to the real libc++ header (do NOT claim its guard).
#include_next <__mutex/lock_guard.h>
#else
#ifndef _LIBCPP___MUTEX_LOCK_GUARD_H
#define _LIBCPP___MUTEX_LOCK_GUARD_H

// intentionally empty

#endif // _LIBCPP___MUTEX_LOCK_GUARD_H
#endif // __wasi__
