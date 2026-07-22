// WASI shim: <__mutex/once_flag.h> — single-threaded no-op on WASI, transparent on native
//
// Adapted from instruction-bounded-inference-artifact
// MIT (c) 2026 Julien Aerni, Simeon Fluck, Dustin Becker
#ifndef __wasi__
// Native: defer entirely to the real libc++ header (do NOT claim its guard).
#include_next <__mutex/once_flag.h>
#else
#ifndef _LIBCPP___MUTEX_ONCE_FLAG_H
#define _LIBCPP___MUTEX_ONCE_FLAG_H


namespace std {

struct once_flag {
    bool called_ = false;
};

template <class Callable, class... Args>
void call_once(once_flag& flag, Callable&& f, Args&&... args) {
    if (!flag.called_) {
        flag.called_ = true;
        f(static_cast<Args&&>(args)...);
    }
}

} // namespace std


#endif // _LIBCPP___MUTEX_ONCE_FLAG_H
#endif // __wasi__
