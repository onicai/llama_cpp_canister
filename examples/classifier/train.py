"""Fine-tunes a small encoder into the 15-score moderation classifier.

    python train.py --base jhu-clsp/ettin-encoder-17m \
        --data data/train.jsonl --out build/model

Holds out 10% of the data for validation, and saves the epoch with the best
validation loss: model and tokenizer, in Hugging Face format, in f32, ready for
llama.cpp's convert_hf_to_gguf.py.
"""

import argparse
import json
import random
from pathlib import Path
from typing import Any, Dict, Iterator, List

import torch
from transformers import (
    AutoModelForSequenceClassification,
    AutoTokenizer,
    get_linear_schedule_with_warmup,
)

from heads import NUM_OUTPUTS, OUTPUTS, SLICES, loss, targets

MAX_LEN = 64  # tokens: a chat message of at most 140 characters fits well within


def batches(
    examples: List[Dict[str, Any]], tokenizer: Any, size: int, shuffle: bool
) -> Iterator[Dict[str, torch.Tensor]]:
    """Tokenized batches with the targets of every fact."""
    order = list(range(len(examples)))
    if shuffle:
        random.shuffle(order)
    for i in range(0, len(order), size):
        chunk = [examples[j] for j in order[i : i + size]]
        enc = tokenizer(
            [e["text"] for e in chunk],
            padding=True,
            truncation=True,
            max_length=MAX_LEN,
            return_tensors="pt",
        )
        ts = [targets(e) for e in chunk]
        enc.update(
            {name: torch.tensor([t[name] for t in ts]) for name, _, _, _ in SLICES}
        )
        yield enc


def evaluate(
    model: Any, examples: List[Dict[str, Any]], tokenizer: Any, dev: torch.device
) -> float:
    """The mean loss over the examples."""
    model.eval()
    total, n = 0.0, 0
    with torch.no_grad():
        for batch in batches(examples, tokenizer, 64, shuffle=False):
            batch = {k: v.to(dev) for k, v in batch.items()}
            logits = model(
                input_ids=batch["input_ids"], attention_mask=batch["attention_mask"]
            ).logits
            total += float(loss(logits, batch)) * len(batch["input_ids"])
            n += len(batch["input_ids"])
    return total / n


def main() -> None:
    """Fine-tunes the base model and saves the best epoch."""
    p = argparse.ArgumentParser()
    p.add_argument("--base", required=True, help="Hugging Face id of the encoder")
    p.add_argument("--data", required=True, type=Path, help="labeled examples, JSONL")
    p.add_argument("--out", required=True, type=Path)
    p.add_argument("--epochs", type=int, default=8)
    p.add_argument("--lr", type=float, default=1e-4)
    p.add_argument("--batch-size", type=int, default=32)
    p.add_argument("--seed", type=int, default=42)
    args = p.parse_args()

    random.seed(args.seed)
    torch.manual_seed(args.seed)
    examples = [json.loads(l) for l in args.data.read_text().splitlines() if l.strip()]
    random.shuffle(examples)
    n_val = max(1, len(examples) // 10)
    val, train = examples[:n_val], examples[n_val:]
    print(f"{len(train)} train, {len(val)} validation examples")

    dev = torch.device("mps" if torch.backends.mps.is_available() else "cpu")
    tokenizer = AutoTokenizer.from_pretrained(args.base)
    model = AutoModelForSequenceClassification.from_pretrained(
        args.base,
        num_labels=NUM_OUTPUTS,
        id2label=dict(enumerate(OUTPUTS)),
        label2id={name: i for i, name in enumerate(OUTPUTS)},
        dtype=torch.float32,
    ).to(dev)
    optimizer = torch.optim.AdamW(model.parameters(), lr=args.lr, weight_decay=0.01)
    steps = args.epochs * ((len(train) + args.batch_size - 1) // args.batch_size)
    schedule = get_linear_schedule_with_warmup(optimizer, int(0.06 * steps), steps)

    best = float("inf")
    for epoch in range(1, args.epochs + 1):
        model.train()
        for batch in batches(train, tokenizer, args.batch_size, shuffle=True):
            batch = {k: v.to(dev) for k, v in batch.items()}
            logits = model(
                input_ids=batch["input_ids"], attention_mask=batch["attention_mask"]
            ).logits
            loss(logits, batch).backward()
            torch.nn.utils.clip_grad_norm_(model.parameters(), 1.0)
            optimizer.step()
            schedule.step()
            optimizer.zero_grad()
        val_loss = evaluate(model, val, tokenizer, dev)
        print(f"epoch {epoch}: validation loss {val_loss:.4f}")
        if val_loss < best:  # keep the best epoch
            best = val_loss
            model.save_pretrained(args.out)
            tokenizer.save_pretrained(args.out)
    print(f"best validation loss {best:.4f}, saved to {args.out}")


if __name__ == "__main__":
    main()
