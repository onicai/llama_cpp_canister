"""Scores a classifier on a held-out test set: the accuracy per fact, and how many
messages get every fact right.

    python eval.py build/model data/test.jsonl            # the PyTorch model
    python eval.py --scores scores.jsonl data/test.jsonl  # scores from elsewhere

--scores reads one JSON list of 15 scores per line, in the order of the test set:
for example what run_classify returned on the canister for each test message, to
check that the canister gets the same messages right.
"""

import argparse
import json
from pathlib import Path
from typing import List

from heads import FACTS, facts, matches


def model_scores(model_dir: Path, texts: List[str]) -> List[List[float]]:
    """The 15 scores of each text, from the PyTorch model."""
    # pylint: disable=import-outside-toplevel
    import torch
    from transformers import AutoModelForSequenceClassification, AutoTokenizer

    tokenizer = AutoTokenizer.from_pretrained(model_dir)
    model = AutoModelForSequenceClassification.from_pretrained(model_dir).eval()
    with torch.no_grad():
        enc = tokenizer(texts, padding=True, return_tensors="pt")
        rows: List[List[float]] = model(**enc).logits.tolist()
    return rows


def main() -> None:
    """Prints the accuracy per fact, and the messages with every fact right."""
    p = argparse.ArgumentParser()
    p.add_argument("model", nargs="?", type=Path, help="a Hugging Face model directory")
    p.add_argument("test", type=Path, help="the test set, JSONL")
    p.add_argument("--scores", type=Path, help="scores per test message, JSONL")
    p.add_argument(
        "--misses", action="store_true", help="list the messages with a wrong fact"
    )
    args = p.parse_args()

    tests = [json.loads(l) for l in args.test.read_text().splitlines() if l.strip()]
    if args.scores:
        rows = [
            json.loads(l) for l in args.scores.read_text().splitlines() if l.strip()
        ]
    else:
        rows = model_scores(args.model, [t["text"] for t in tests])
    assert len(rows) == len(tests), "one row of scores per test message"

    right = {fact: 0 for fact in FACTS}
    all_right = 0
    for test, row in zip(tests, rows):
        f = facts(row)
        wrong = [fact for fact in FACTS if not matches(fact, f[fact], test)]
        for fact in FACTS:
            right[fact] += fact not in wrong
        all_right += not wrong
        if wrong and args.misses:
            print(f"  {test.get('id', '')}: {', '.join(wrong)} | {test['text']}")
    print(" ".join(f"{fact} {right[fact] / len(tests):.2f}" for fact in FACTS))
    print(f"all facts right: {all_right}/{len(tests)}")


if __name__ == "__main__":
    main()
