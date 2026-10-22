"""Builds the training set from two independent labeling passes.

    python merge_labels.py export raw/ relabel/          # gen-*.jsonl -> in-*.txt
    python merge_labels.py merge raw/ relabel/ test.jsonl train.jsonl

Pass A: the generator of a message labels it (raw/gen-*.jsonl). Pass B: a second
labeler sees only the texts (relabel/in-*.txt) and labels them again, following
the written moderation policy (relabel/out-*.jsonl). A message is kept when both
passes agree on what it is: the same kind, scam and misleading (not compared
for a scam: the policy makes every scam misleading), and toxicity within one
level. It keeps pass B's labels, so that respectful and action follow
the policy everywhere. Duplicates, and every text that is also in the test set,
are dropped.
"""

import json
import re
import sys
from pathlib import Path
from typing import Any, Dict, List, Set

N_RELABEL_FILES = 3
SAME = ("kind", "scam", "misleading")


def norm(text: str) -> str:
    """The text with case, punctuation and spacing removed, to find duplicates."""
    key = re.sub(r"[^a-z0-9]+", " ", text.lower()).strip()
    return key or text.strip().lower()  # emoji- or punctuation-only messages


def read_jsonl(path: Path) -> List[Dict[str, Any]]:
    """One JSON object per line."""
    return [json.loads(l) for l in path.read_text().splitlines() if l.strip()]


def generated(raw: Path, held_out: Set[str]) -> List[Dict[str, Any]]:
    """Pass A, without duplicates and without test-set texts."""
    seen: Set[str] = set()
    out = []
    for path in sorted(raw.glob("gen-*.jsonl")):
        for e in read_jsonl(path):
            key = norm(e["text"])
            if key and key not in seen and key not in held_out:
                seen.add(key)
                out.append(e)
    return out


def export(raw: Path, relabel: Path) -> None:
    """Writes the texts of pass A for pass B, interleaved over the files."""
    relabel.mkdir(parents=True, exist_ok=True)
    texts = [e["text"] for e in generated(raw, set())]
    for k in range(N_RELABEL_FILES):
        (relabel / f"in-{k + 1}.txt").write_text(
            "\n".join(texts[k::N_RELABEL_FILES]) + "\n"
        )
    print(f"{len(texts)} texts into {N_RELABEL_FILES} files in {relabel}")


def merge(raw: Path, relabel: Path, test: Path, out: Path) -> None:
    """Keeps the messages both passes agree on."""
    held_out = {norm(e["text"]) for e in read_jsonl(test)}
    b = {
        e["text"]: e
        for path in sorted(relabel.glob("out-*.jsonl"))
        for e in read_jsonl(path)
    }
    kept: List[Dict[str, Any]] = []
    disagree = {k: 0 for k in SAME + ("toxicity",)}
    missing = 0
    for a in generated(raw, held_out):
        other = b.get(a["text"])
        if other is None:
            missing += 1
            continue
        bad = [k for k in SAME if a[k] != other[k]]
        if a["scam"] and other["scam"] and "misleading" in bad:
            bad.remove("misleading")  # the policy makes every scam misleading
        if abs(int(a["toxicity"]) - int(other["toxicity"])) > 1:
            bad.append("toxicity")
        for k in bad:
            disagree[k] += 1
        if not bad:
            labels = {k: v for k, v in other.items() if k != "text"}
            if labels["scam"]:
                labels["misleading"] = True  # the policy makes every scam misleading
            kept.append({"text": a["text"], **labels})
    out.write_text("".join(json.dumps(e) + "\n" for e in kept))
    print(f"kept {len(kept)}, without a second label {missing}")
    print(f"disagreements per fact: {disagree}")


if __name__ == "__main__":
    if sys.argv[1] == "export":
        export(Path(sys.argv[2]), Path(sys.argv[3]))
    else:
        merge(
            Path(sys.argv[2]), Path(sys.argv[3]), Path(sys.argv[4]), Path(sys.argv[5])
        )
