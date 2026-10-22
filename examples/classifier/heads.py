"""The classifier's outputs: one linear head of 15 scores, a group per fact.

A single head keeps the model a standard ...ForSequenceClassification, which
llama.cpp converts to GGUF and runs in one forward pass. The labels below become
the model's id2label, the GGUF's classifier labels, and the `labels` that
run_classify returns, in this order.
"""

from typing import Any, Dict, List

import torch

KINDS = ["question", "discussion", "support", "troll", "spam"]
TOXICITY = 3  # levels 0 civil, 1 rude, 2 abusive
ACTIONS = ["none", "warn", "delete", "block"]

# (fact, first index, size, kind): "softmax" over the group, or "sigmoid" for one score
SLICES = [
    ("kind", 0, len(KINDS), "softmax"),
    ("toxicity", 5, TOXICITY, "softmax"),
    ("scam", 8, 1, "sigmoid"),
    ("misleading", 9, 1, "sigmoid"),
    ("respectful", 10, 1, "sigmoid"),
    ("action", 11, len(ACTIONS), "softmax"),
]
OUTPUTS: List[str] = (
    [f"kind:{k}" for k in KINDS]
    + [f"toxicity:{n}" for n in range(TOXICITY)]
    + ["scam", "misleading", "respectful"]
    + [f"action:{a}" for a in ACTIONS]
)
NUM_OUTPUTS = len(OUTPUTS)
FACTS = [name for name, _, _, _ in SLICES]
assert NUM_OUTPUTS == 15 and SLICES[-1][1] + SLICES[-1][2] == NUM_OUTPUTS


def targets(example: Dict[str, Any]) -> Dict[str, int]:
    """The class index, or 0/1, per fact of a labeled example."""
    return {
        "kind": KINDS.index(example["kind"]),
        "toxicity": int(example["toxicity"]),
        "scam": int(bool(example["scam"])),
        "misleading": int(bool(example["misleading"])),
        "respectful": int(bool(example["respectful"])),
        "action": ACTIONS.index(example["action"]),
    }


def loss(logits: torch.Tensor, batch: Dict[str, torch.Tensor]) -> torch.Tensor:
    """The sum of the per-fact losses: cross-entropy per softmax group, BCE per flag."""
    total = logits.new_zeros(())
    for name, start, size, kind in SLICES:
        part = logits[:, start : start + size]
        if kind == "softmax":
            total = total + torch.nn.functional.cross_entropy(part, batch[name])
        else:
            total = total + torch.nn.functional.binary_cross_entropy_with_logits(
                part[:, 0], batch[name].float()
            )
    return total


def facts(scores: List[float]) -> Dict[str, Any]:
    """The facts of one row of scores, as a caller reads them: the most likely value
    of each group with its probability, and P(yes) of each flag."""
    out: Dict[str, Any] = {}
    row = torch.tensor(scores)
    for name, start, size, kind in SLICES:
        part = row[start : start + size]
        if kind == "softmax":
            p = torch.softmax(part, dim=-1)
            top = int(p.argmax())
            out[name] = {"kind": KINDS, "action": ACTIONS}.get(name, list(range(size)))[
                top
            ]
            out[name + "_p"] = float(p[top])
        else:
            out[name] = float(torch.sigmoid(part[0]))
    return out


def matches(fact: str, value: Any, example: Dict[str, Any]) -> bool:
    """Whether a value read by facts() is the labeled value of an example."""
    if fact in ("scam", "misleading", "respectful"):
        return (value > 0.5) == bool(example[fact])
    return bool(value == example[fact])
