"""Print a gguf's architecture and chat template without loading the model.

Needed because the canister applies NO chat template
(LLAMA_ICP_NO_CHAT_TEMPLATES), so every candidate's prompt must be hand-written
in its own format. Guessing the format produces output that looks exactly like a
bad model - read it from the file instead.

Usage:
  python scripts/gguf_meta.py <model.gguf> [--full]
"""

import re
import struct
import sys
from typing import Any, BinaryIO, Dict, Sequence

KEYS = (
    "general.architecture",
    "general.name",
    "tokenizer.chat_template",
    "tokenizer.ggml.bos_token_id",
    "tokenizer.ggml.eos_token_id",
    "tokenizer.ggml.add_bos_token",
)


def read_kv(path: str, keys: Sequence[str] = KEYS) -> Dict[str, Any]:
    """Return the requested GGUF key/value metadata entries as a dict."""
    with open(path, "rb") as f:
        return _read_kv(f, keys)


def _read_kv(f: BinaryIO, keys: Sequence[str]) -> Dict[str, Any]:
    """Parse the GGUF KV header from an open binary file object."""
    if f.read(4) != b"GGUF":
        raise ValueError("not a gguf file")
    struct.unpack("<I", f.read(4))  # version
    struct.unpack("<Q", f.read(8))  # n_tensors
    (n_kv,) = struct.unpack("<Q", f.read(8))

    def rd(t: int) -> Any:  # pylint: disable=too-many-return-statements
        if t == 0:
            return struct.unpack("<B", f.read(1))[0]
        if t == 1:
            return struct.unpack("<b", f.read(1))[0]
        if t == 2:
            return struct.unpack("<H", f.read(2))[0]
        if t == 3:
            return struct.unpack("<h", f.read(2))[0]
        if t == 4:
            return struct.unpack("<I", f.read(4))[0]
        if t == 5:
            return struct.unpack("<i", f.read(4))[0]
        if t == 6:
            return struct.unpack("<f", f.read(4))[0]
        if t == 7:
            return struct.unpack("<?", f.read(1))[0]
        if t == 8:
            (n,) = struct.unpack("<Q", f.read(8))
            return f.read(n).decode("utf-8", "replace")
        if t == 9:
            (et,) = struct.unpack("<I", f.read(4))
            (n,) = struct.unpack("<Q", f.read(8))
            return [rd(et) for _ in range(n)]
        if t == 10:
            return struct.unpack("<Q", f.read(8))[0]
        if t == 11:
            return struct.unpack("<q", f.read(8))[0]
        if t == 12:
            return struct.unpack("<d", f.read(8))[0]
        raise ValueError(f"unknown gguf value type {t}")

    found = {}
    for _ in range(n_kv):
        (kl,) = struct.unpack("<Q", f.read(8))
        key = f.read(kl).decode("utf-8", "replace")
        (vt,) = struct.unpack("<I", f.read(4))
        val = rd(vt)
        if key in keys:
            found[key] = val
    return found


def main() -> int:
    """CLI entry point: print arch and chat template for a gguf path."""
    path = sys.argv[1]
    full = "--full" in sys.argv
    kv = read_kv(path)
    print(f"architecture : {kv.get('general.architecture')}")
    print(f"name         : {kv.get('general.name')}")
    print(
        f"bos/eos id   : {kv.get('tokenizer.ggml.bos_token_id')} / "
        f"{kv.get('tokenizer.ggml.eos_token_id')}"
    )
    print(f"add_bos      : {kv.get('tokenizer.ggml.add_bos_token')}")
    t = kv.get("tokenizer.chat_template", "")
    if not t:
        print("\nchat_template: (none)")
        return 0
    print("\n--- chat_template " + ("(full)" if full else "(key parts)") + " ---")
    if full:
        print(t)
        return 0
    # The generation prompt is what matters: it is the suffix we must reproduce.
    m = re.search(r"add_generation_prompt.{0,400}", t, re.S)
    if m:
        print("[generation prompt]\n" + m.group(0)[:400] + "\n")
    for tag in ("<think", "role", "system"):
        hits = list(re.finditer(r".{0,80}" + tag + r".{0,120}", t, re.S))[:2]
        for h in hits:
            print(f"[{tag}] ..." + h.group(0).replace("\n", " ")[:200])
    return 0


if __name__ == "__main__":
    sys.exit(main())
