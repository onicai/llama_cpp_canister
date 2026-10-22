# Train a sequence classifier for run_classify

This guide fine-tunes a small encoder into a **sequence classifier** and runs it on
the canister with `run_classify`: one text in, one raw score per label out, in one
forward pass. Read [README-classifier-models.md](README-classifier-models.md) first:
it covers the endpoint, the load arguments and what a call costs.

The Ettin-encoder-17M model in that README was trained this way. Its 2,670 labeled
examples gave 38 of 46 held-out messages fully right, where a generic 0.8B decision
model got 18.

The steps:

1. Design the outputs: one head, with a group of scores per fact.
2. Label data, and keep a test set apart.
3. Fine-tune with Hugging Face `transformers`.
4. Evaluate on the test set.
5. Convert to GGUF, f32.
6. Check that llama.cpp gives the scores PyTorch gives.
7. Deploy, and check it again on the canister.

The example task throughout is support-message triage: for each message, its
`intent` (refund, cancel, track, other), its `urgency` (0-2) and whether the customer
is `angry`. Replace it with your own facts.

## 1. Choose a base model

Use an **encoder** (bidirectional attention) that llama.cpp can run as a
`...ForSequenceClassification` model. We tested ModernBERT:

| Base model                    | Parameters | Notes                                                     |
|-------------------------------|------------|-----------------------------------------------------------|
| `jhu-clsp/ettin-encoder-17m`  | 17 M       | English; tested on the canister (~25 M instructions per token in f32) |
| `jhu-clsp/ettin-encoder-32m`  | 32 M       | English; same architecture, more instructions per token   |
| `jhu-clsp/mmBERT-small`       | 140 M      | multilingual (the base of Julia-1)                        |

The cost per token grows with the size of the layers. 13 M of Ettin-17M's parameters
are its token-embedding table, which costs almost nothing. Measure a larger model
before you rely on it (see step 7).

## 2. Design the outputs: one head, a group per fact

llama.cpp runs **one** classification head and returns all of its scores. To read
several facts from one message in one pass, give the head one group of scores per
fact:

- a fact with one of several values (`intent`): one score per value, softmax over the
  group;
- a level (`urgency` 0-2): one score per level, softmax over the group;
- a yes/no fact (`angry`): one score, sigmoid.

Name every score `fact:value`. The names become the model's `id2label`, the GGUF's
labels, and the `labels` that `run_classify` returns, in order. That way a caller can
check the order it reads.

The labels are required: `load_model` only loads a model as a classifier when its GGUF
has them. The converter writes them from `id2label`, but drops the default
`LABEL_0`/`LABEL_1` of a 2-label model, so always set real names, as `train.py` below
does.

```python
# heads.py: the output layout, shared by training, evaluation and the caller
from typing import Any, Dict, List

import torch

INTENTS = ["refund", "cancel", "track", "other"]
URGENCY = 3  # levels 0-2

# (fact, first index, size, kind)
SLICES = [
    ("intent", 0, len(INTENTS), "softmax"),
    ("urgency", 4, URGENCY, "softmax"),
    ("angry", 7, 1, "sigmoid"),
]
OUTPUTS: List[str] = (
    [f"intent:{i}" for i in INTENTS] + [f"urgency:{n}" for n in range(URGENCY)] + ["angry"]
)
NUM_OUTPUTS = len(OUTPUTS)


def targets(example: Dict[str, Any]) -> Dict[str, int]:
    """The class index, or 0/1, per fact of a labeled example."""
    return {
        "intent": INTENTS.index(example["intent"]),
        "urgency": int(example["urgency"]),
        "angry": int(bool(example["angry"])),
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
    """The facts of one row of scores: the top value and its probability, or P(yes)."""
    out: Dict[str, Any] = {}
    row = torch.tensor(scores)
    for name, start, size, kind in SLICES:
        part = row[start : start + size]
        if kind == "softmax":
            p = torch.softmax(part, dim=-1)
            out[name] = int(p.argmax()) if name != "intent" else INTENTS[int(p.argmax())]
            out[name + "_p"] = float(p.max())
        else:
            out[name] = float(torch.sigmoid(part[0]))
    return out
```

Do **not** use the standard `problem_type` loss of `transformers` for this. It treats
all scores as one softmax, or all as independent sigmoids. The custom `loss` above
trains each group as what it is.

## 3. Label the data

One JSON object per line, the text and every fact:

```json
{"text": "I was charged twice, refund me NOW", "intent": "refund", "urgency": 2, "angry": true}
{"text": "where is my parcel?", "intent": "track", "urgency": 1, "angry": false}
```

What worked:

- **Volume:** a few thousand examples. Ettin-17M used 2,670.
- **Two independent labels per text.** One pass writes a text together with its
  labels. A second labeler sees only the text and labels it again. Keep a text only
  when both agree: the same value for a choice or a flag, and levels within one step
  of each other (keep the first pass's level). This drops ambiguous examples, which
  would otherwise teach the model noise.
- **Cover every value of every fact.** Include the hard cases: polite but angry, a
  question that mentions a refund without asking for one, typos, emoji, one word.
- **The length you will serve.** Train on texts as long as the ones the canister will
  get. That length also sets `-ub` at load time.
- **A held-out test set, never trained on.** A few dozen hand-checked messages with
  the facts they must give. Remove every training text that is also in it, after
  normalizing case and punctuation.

## 4. Fine-tune

Python 3.11+, with `torch` and `transformers`. A Mac uses the `mps` device, and a CPU
works too: a 17 M model trains in minutes.

```python
# train.py: python train.py --base jhu-clsp/ettin-encoder-17m --data train.jsonl --out out/model
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

MAX_LEN = 64  # tokens: longer than any text you train on


def batches(examples: List[Dict[str, Any]], tokenizer: Any, size: int,
            shuffle: bool) -> Iterator[Dict[str, torch.Tensor]]:
    order = list(range(len(examples)))
    if shuffle:
        random.shuffle(order)
    for i in range(0, len(order), size):
        chunk = [examples[j] for j in order[i : i + size]]
        enc = tokenizer([e["text"] for e in chunk], padding=True, truncation=True,
                        max_length=MAX_LEN, return_tensors="pt")
        ts = [targets(e) for e in chunk]
        enc.update({name: torch.tensor([t[name] for t in ts]) for name, _, _, _ in SLICES})
        yield enc


def evaluate(model: Any, examples: List[Dict[str, Any]], tokenizer: Any,
             dev: torch.device) -> float:
    model.eval()
    total, n = 0.0, 0
    with torch.no_grad():
        for batch in batches(examples, tokenizer, 64, shuffle=False):
            batch = {k: v.to(dev) for k, v in batch.items()}
            logits = model(input_ids=batch["input_ids"],
                           attention_mask=batch["attention_mask"]).logits
            total += float(loss(logits, batch)) * len(batch["input_ids"])
            n += len(batch["input_ids"])
    return total / n


def main() -> None:
    p = argparse.ArgumentParser()
    p.add_argument("--base", required=True, help="Hugging Face id of the encoder")
    p.add_argument("--data", required=True, type=Path, help="labeled examples, JSONL")
    p.add_argument("--out", required=True, type=Path)
    p.add_argument("--epochs", type=int, default=8)
    p.add_argument("--lr", type=float, default=5e-5)
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
    ).to(dev)
    optimizer = torch.optim.AdamW(model.parameters(), lr=args.lr, weight_decay=0.01)
    steps = args.epochs * ((len(train) + args.batch_size - 1) // args.batch_size)
    schedule = get_linear_schedule_with_warmup(optimizer, int(0.06 * steps), steps)

    best = float("inf")
    for epoch in range(1, args.epochs + 1):
        model.train()
        for batch in batches(train, tokenizer, args.batch_size, shuffle=True):
            batch = {k: v.to(dev) for k, v in batch.items()}
            logits = model(input_ids=batch["input_ids"],
                           attention_mask=batch["attention_mask"]).logits
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
```

The saved directory is a standard Hugging Face model (`config.json`,
`model.safetensors`, the tokenizer). Its `id2label` holds your `fact:value` names.

## 5. Evaluate on the held-out test set

Score each fact separately, and count the messages that get every fact right. That
count is the number to improve.

```python
# eval.py: python eval.py out/model test.jsonl   (the test set in the format of step 3)
import json
import sys

import torch
from transformers import AutoModelForSequenceClassification, AutoTokenizer

from heads import SLICES, facts

model_dir, test_path = sys.argv[1], sys.argv[2]
tokenizer = AutoTokenizer.from_pretrained(model_dir)
model = AutoModelForSequenceClassification.from_pretrained(model_dir).eval()
tests = [json.loads(l) for l in open(test_path) if l.strip()]
with torch.no_grad():
    enc = tokenizer([t["text"] for t in tests], padding=True, return_tensors="pt")
    rows = model(**enc).logits.tolist()

names = [name for name, _, _, _ in SLICES]
right = {name: 0 for name in names}
all_right = 0
for test, row in zip(tests, rows):
    f = facts(row)
    ok = {name: (f[name] > 0.5) == test[name] if name == "angry" else f[name] == test[name]
          for name in names}
    for name in names:
        right[name] += ok[name]
    all_right += all(ok.values())
print({name: round(right[name] / len(tests), 2) for name in names},
      f"all right: {all_right}/{len(tests)}")
```

To improve the result, add labeled examples where the model is wrong, try the next
base model up, or train for more epochs. Change one thing at a time, and always
compare on the same test set.

## 6. Convert to GGUF, f32

Use `convert_hf_to_gguf.py` from llama.cpp. The fork in this repo has it, at
`src/llama_cpp_onicai_fork`. It needs `torch`, `transformers` and `numpy`, which the
training environment already has:

```bash
python src/llama_cpp_onicai_fork/convert_hf_to_gguf.py out/model --outtype f32 --outfile models/my-classifier-f32.gguf
```

**Use `--outtype f32`, not f16.** In wasm, f32 runs on SIMD, while f16 converts every
weight one by one as it reads it. An f32 model is 3.6x cheaper per token with the same
scores (see [README-classifier-models.md](README-classifier-models.md)).

The GGUF holds:
- the labels, as `<arch>.classifier.output_labels`;
- the pooling, as `<arch>.classifier.pooling_type`, from `classifier_pooling` in
  `config.json`;
- the head tensors `cls.*`.

## 7. Check the GGUF against PyTorch

Build `llama-embedding` from the fork once, outside the source tree:

```bash
cmake -S src/llama_cpp_onicai_fork -B build-llama -DCMAKE_BUILD_TYPE=Release -DLLAMA_CURL=OFF -DGGML_METAL=OFF
```

```bash
cmake --build build-llama --target llama-embedding llama-quantize -j 8
```

(CPU only, like the canister.) Then compare its scores with PyTorch's for a few texts.
This is what `run_classify` computes on the canister:

```bash
build-llama/bin/llama-embedding -m models/my-classifier-f32.gguf --pooling rank --embd-normalize -1 -p "I was charged twice, refund me NOW"
```

```python
import torch
from transformers import AutoModelForSequenceClassification, AutoTokenizer

tokenizer = AutoTokenizer.from_pretrained("out/model")
model = AutoModelForSequenceClassification.from_pretrained("out/model").eval()
with torch.no_grad():
    print(model(**tokenizer("I was charged twice, refund me NOW", return_tensors="pt")).logits[0])
```

The scores agree within about 0.02. For Ettin-17M every score was within 0.015 of
PyTorch's. A larger difference means the conversion went wrong. Check that the
labels came out in the right order, and that `classifier_pooling` in `config.json`
matches the pooling the model was trained with.

## 8. Deploy and check on the canister

Upload the GGUF and load it as in [README-classifier-models.md](README-classifier-models.md):

```bash
python -m scripts.upload --network local --canister llama_cpp --canister-filename models/model.gguf models/my-classifier-f32.gguf
```

```bash
icp canister call llama_cpp load_model '(record { args = vec {"--model"; "models/model.gguf"; "--embedding"; "--pooling"; "rank"; "--no-warmup"; "-c"; "192"; "-ub"; "192"} })' -e local
```

Set `-c` and `-ub` to the longest text you serve, in tokens with the special tokens,
and check the cost:

- `run_classify` returns `input_tokens` and `instructions` for every call;
- a text that fills `-ub` exactly is the most expensive one, and it must fit:
  - **5 B instructions** for `run_classify_query`;
  - **40 B instructions** for `run_classify`;
- for Ettin-17M in f32 that is `-ub 192`, at ~25 M instructions per token. A larger
  model costs more per token, so lower `-ub` until its longest text fits.

Finally, run the held-out test set through `run_classify`, with your caller's own
`facts` logic, and check that the canister gets the same messages right as step 5.
That is the acceptance test.

## 9. Read the scores in the caller

`run_classify` returns the raw scores, in the order of `labels`. Apply what the model
was trained with:
- softmax over each group, and take the top value;
- sigmoid for each flag.

That is `facts` in `heads.py` above. In a canister written in Motoko, Rust or C++, port
those few lines and keep the `SLICES` layout in one place.

Check `labels` against the layout once, when the caller starts or after a model
change. Then a model trained with a different layout fails loudly instead of being
read wrongly.
