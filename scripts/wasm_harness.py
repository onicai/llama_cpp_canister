"""Faithful wasmtime harness for debugging llama_cpp_canister traps.

The IC gives NO wasm backtrace for an install/runtime trap -- only
`[TRAP]: heap out of bounds`. This harness runs the canister wasm under wasmtime
with faithful `ic0` host functions, so you get a real (and, on the right wasm, a
NAMED) backtrace. It found every wasm-only bug in the b10076 upgrade.

Key ideas (see README-0003-305ba519.md "Debugging technique"):

  * Stable memory is backed by a real Python bytearray, NOT no-op zeros, so the
    ic-wasi-polyfill runs its real BTreeMap / stable-fs init and behaves as on
    the IC.
  * Run the PRE-optimize wasm (build/llama_cpp_before_opt_internal.wasm) to get NAMED
    functions in the backtrace -- binaryen's optimize() strips the wasm name
    section, so the deployed build/llama_cpp.wasm shows only <wasm function N>.
    wasmtime does not care that it still exports globals the IC rejects.
    (icpp-pro 6.2.0+ writes this backup itself, as part of its built-in
    globals-limit fix -- hence the _internal suffix.)
  * Instantiating runs the start section (the C++ ctors) -- that catches
    static-init faults. To catch faults deeper in a canister method, pass
    --method and (for admin-gated methods) rely on is_controller being forced
    to 1.

INTERPRETATION RULE: if this harness says the binary is clean but the IC traps,
suspect the DEPLOY PIPELINE (e.g. a stale icp `.icp/cache/` cached wasm), not the
binary. Always redeploy with `icp deploy --mode reinstall` and check the module
hash (`icp canister status <name>`).

Requirements:  pip install wasmtime
Encode a candid arg with:  didc encode '(record { ... })'   (hex output)

Examples
--------
# Just instantiate (runs ctors); named backtrace on any static-init trap:
python -m scripts.wasm_harness build/llama_cpp_before_opt_internal.wasm

# Call load_model (needs the small candid arg; admin auth is forced to pass):
didc encode '(record { args = vec {"--model"; "models/tiny.gguf";} })' > /tmp/load.hex
python -m scripts.wasm_harness build/llama_cpp_before_opt_internal.wasm \
    --method 'canister_update load_model' --arg-hex-file /tmp/load.hex

# MULTI-CALL sessions: --method/--arg-hex-file are repeatable and are zipped
# into (method, arg) pairs, all run against ONE instantiation. The wasm heap
# and the stable bytearray persist across the calls, so this reproduces the
# IC's Orthogonal Persistence: a llama_context created by call 1 is the SAME
# object reused by calls 2..N (the cross-call bug surface of
# README-0003-305ba519-IC0502.md). Pass '-' as an --arg-hex-file for an empty
# arg. Each call's reply bytes (captured from msg_reply_data_append) are
# printed; a trap reports the 1-based call index plus the named backtrace.
python -m scripts.wasm_harness build/llama_cpp_before_opt_internal.wasm \
    --method 'canister_update load_model'  --arg-hex-file /tmp/load.hex \
    --method 'canister_update run_update'  --arg-hex-file /tmp/run.hex \
    --method 'canister_update run_update'  --arg-hex-file /tmp/run.hex

Note: methods that read a model file will fault on "file not found" unless you
first upload one into the harness vFS (call file_upload_chunk the same way, with
a candid blob arg). getenv-class faults surface at arg-parse, BEFORE any file
access, so they reproduce without a model.
"""

import argparse
import binascii
import ctypes
import sys
from typing import Any, Callable, Tuple

import wasmtime


def build_linker(
    store: wasmtime.Store, module: wasmtime.Module, msg: dict[str, Any]
) -> Tuple[wasmtime.Linker, bytearray]:
    """Wire faithful ic0 host functions. Stable memory = a real bytearray.

    `msg` is the mutable per-call message context, updated by the caller
    between calls on the same instance: msg["arg"] holds the current call's
    candid arg bytes, msg["reply"] accumulates this call's reply bytes
    (captured from msg_reply_data_append; clear it before each call).
    """
    linker = wasmtime.Linker(store.engine)
    stable = bytearray()

    # Memory is accessed via the Caller so it works during the start section
    # (which runs before instantiate() returns and before we hold a memory ref).
    def read_mem(caller: Any, off: int, n: int) -> bytes:
        mem = caller.get("memory")
        base = mem.data_ptr(store)
        return bytes(
            (ctypes.c_ubyte * n).from_address(ctypes.addressof(base.contents) + off)
        )

    def write_mem(caller: Any, off: int, data: bytes) -> None:
        mem = caller.get("memory")
        base = mem.data_ptr(store)
        ctypes.memmove(ctypes.addressof(base.contents) + off, data, len(data))

    def grow(_caller: Any, pages: int) -> int:
        old = len(stable) // 65536
        stable.extend(b"\x00" * (pages * 65536))
        return old

    caller_principal = b"\x01"  # any non-anonymous principal; auth forced below

    def debug_print(caller: Any, src: int, size: int) -> None:
        # Must NOT return a value. sys.stderr.write() returns an int (chars
        # written); as a *statement* here its result is discarded and the
        # function returns None. Returning it (e.g. from a lambda) trips
        # wasmtime's "callback produced results when it shouldn't".
        sys.stderr.write(
            "[canister] " + read_mem(caller, src, size).decode("utf-8", "replace")
        )

    handlers: dict[str, Callable[..., Any]] = {
        # --- stable memory (faithful: backed by `stable`) ---
        "stable64_size": lambda c: len(stable) // 65536,
        "stable64_grow": grow,
        "stable64_write": lambda c, off, src, sz: stable.__setitem__(
            slice(off, off + sz), read_mem(c, src, sz)
        ),
        "stable64_read": lambda c, dst, off, sz: write_mem(
            c, dst, bytes(stable[off : off + sz])
        ),
        "stable_size": lambda c: len(stable) // 65536,
        "stable_grow": grow,
        "stable_write": lambda c, off, src, sz: stable.__setitem__(
            slice(off, off + sz), read_mem(c, src, sz)
        ),
        "stable_read": lambda c, dst, off, sz: write_mem(
            c, dst, bytes(stable[off : off + sz])
        ),
        # --- message context (realistic-ish values; msg is per-call mutable) ---
        "time": lambda c: 1753000000000000000,
        "msg_arg_data_size": lambda c: len(msg["arg"]),
        "msg_arg_data_copy": lambda c, dst, off, sz: write_mem(
            c, dst, msg["arg"][off : off + sz]
        ),
        "msg_caller_size": lambda c: len(caller_principal),
        "msg_caller_copy": lambda c, dst, off, sz: write_mem(
            c, dst, caller_principal[off : off + sz]
        ),
        "is_controller": lambda c, src, sz: 1,  # force admin/controller auth to pass
        "msg_reply_data_append": lambda c, src, sz: msg["reply"].extend(
            read_mem(c, src, sz)
        ),
        "msg_reply": lambda c: None,
        # --- diagnostics ---
        "debug_print": debug_print,
        "trap": lambda c, src, sz: _do_trap(read_mem(c, src, sz) if sz else b""),
    }

    for imp in module.imports:
        name = imp.name
        if (
            imp.module != "ic0"
            or name is None
            or not isinstance(imp.type, wasmtime.FuncType)
        ):
            continue
        functype = imp.type
        if name in handlers:
            linker.define_func(
                "ic0", name, functype, handlers[name], access_caller=True
            )
        else:
            # Generic stub: return zeros matching the result arity, or None for
            # a void function (returning a value for a void func is the classic
            # "callback produced results when it shouldn't" wasmtime error).
            n_res = len(functype.results)
            linker.define_func(
                "ic0", name, functype, _make_stub(n_res), access_caller=True
            )
    return linker, stable


def _make_stub(n_res: int) -> Callable[..., Any]:
    """Build a zero-returning ic0 stub matching the given result arity."""

    def stub(_caller: Any, *_args: Any) -> Any:
        if n_res == 0:
            return None
        if n_res == 1:
            return 0
        return tuple(0 for _ in range(n_res))

    return stub


def _do_trap(msg_bytes: bytes) -> None:
    raise wasmtime.Trap("ic0.trap: " + msg_bytes.decode("utf-8", "replace"))


def main() -> None:
    """CLI entry point: parse args, run the wasm, print the trap backtrace."""
    parser = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter
    )
    parser.add_argument(
        "wasm", help="path to the wasm (use *_before_opt_internal.wasm for NAMES)"
    )
    parser.add_argument(
        "--method",
        action="append",
        help="exported method to call, e.g. 'canister_update load_model'. "
        "Repeatable: all calls run against ONE instantiation, so the wasm "
        "heap (and any persisted llama_context) survives between them "
        "(default: just instantiate)",
    )
    parser.add_argument(
        "--arg-hex-file",
        action="append",
        help="file with the hex candid arg (from `didc encode ...`), one per "
        "--method in order; '-' = empty arg",
    )
    args = parser.parse_args()

    methods = args.method or []
    arg_files = args.arg_hex_file or []
    if len(arg_files) not in (0, len(methods)):
        parser.error(
            f"{len(methods)} --method but {len(arg_files)} --arg-hex-file; "
            "pass one per method (use '-' for an empty arg) or none at all"
        )

    def load_arg(path: str) -> bytes:
        if path == "-":
            return b""
        with open(path, encoding="utf-8") as hexfile:
            return binascii.unhexlify(hexfile.read().strip())

    calls = [
        (m, load_arg(arg_files[i]) if arg_files else b"") for i, m in enumerate(methods)
    ]

    cfg = wasmtime.Config()
    # wasmtime's type stubs omit this attribute, but it works at runtime and
    # gives fuller (named) backtraces.
    cfg.wasm_backtrace_details = True  # type: ignore[attr-defined]
    store = wasmtime.Store(wasmtime.Engine(cfg))
    module = wasmtime.Module.from_file(store.engine, args.wasm)
    msg: dict[str, Any] = {"arg": b"", "reply": bytearray()}
    linker, stable = build_linker(store, module, msg)

    index = 0  # 0 = instantiation; 1.. = the corresponding --method call
    try:
        # Instantiation runs the wasm start section = the C++ ctors (post-wasi2ic).
        inst = linker.instantiate(store, module)
        pages = len(stable) // 65536
        print(
            f"=== instantiated OK (ctors ran clean); stable pages: {pages} ===",
            file=sys.stderr,
        )
        for index, (method, arg_bytes) in enumerate(calls, start=1):
            print(f"=== call {index}/{len(calls)}: {method!r} ===", file=sys.stderr)
            msg["arg"] = arg_bytes
            msg["reply"] = bytearray()
            export = inst.exports(store)[method]
            assert isinstance(export, wasmtime.Func)
            export(store)
            reply = bytes(msg["reply"])
            print(
                f"=== call {index} returned OK; reply {len(reply)} bytes: "
                f"{reply.decode('utf-8', 'replace')[:500]!r} ===",
                file=sys.stderr,
            )
        print(f"=== OK ({len(calls)} call(s), no trap) ===")
    except Exception as exc:  # pylint: disable=broad-except
        where = (
            "instantiation" if index == 0 else f"call {index}: {calls[index - 1][0]!r}"
        )
        print(f"=== TRAP during {where} ===")
        print(str(exc)[:3000])
        sys.exit(1)


if __name__ == "__main__":
    main()
