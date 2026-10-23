# Train a sequence classifier for run_classify

This guide fine-tunes a small encoder into a **sequence classifier** and runs it on the
canister with `run_classify`: one text in, one raw score per label out, in one forward
pass. Read [README-classifier-models.md](README-classifier-models.md) first: it covers the
endpoint, the load arguments and what a call costs.

The worked example is a **moderation classifier for the public chat channel of a DAO**:
for each message it scores what kind of message it is, how toxic, whether it is a scam,
misleading or respectful, and which moderator action fits. Everything is runnable:

- the code: [examples/classifier](../examples/classifier) (`heads.py`, `train.py`,
  `eval.py`, `merge_labels.py`, `Makefile`);
- the data: [onicai/dao-moderation-classifier](https://huggingface.co/datasets/onicai/dao-moderation-classifier)
  on Hugging Face, 3,381 training and 200 test messages, synthetic;
- the model: [onicai/dao-moderation-classifier](https://huggingface.co/onicai/dao-moderation-classifier).

```bash
cd examples/classifier
pip install -r requirements.txt     # a separate environment from the canister's
make data train eval gguf           # download, fine-tune, evaluate, convert to f32 GGUF
```

On a laptop (Apple M-series, `mps`) training takes a few minutes.

The steps:

1. Choose a base model.
2. Design the outputs: one head, a group of scores per fact.
3. Write the labeling guidelines, label the data twice, and keep a test set apart.
4. Fine-tune.
5. Evaluate on the test set, and improve.
6. Convert to GGUF, f32.
7. Check that llama.cpp gives the scores PyTorch gives.
8. Deploy, and check it again on the canister.
9. Read the scores in the caller.

## 1. Choose a base model

Use an **encoder** (bidirectional attention) that llama.cpp runs as a
`...ForSequenceClassification` model. We tested ModernBERT:

| Base model                    | Parameters | On this task                                                                        |
|-------------------------------|------------|-------------------------------------------------------------------------------------|
| `jhu-clsp/ettin-encoder-17m`  | 17 M       | **the example's model**: ~25 M instructions per token in f32; 192 tokens fit a query |
| `jhu-clsp/ettin-encoder-32m`  | 32 M       | the same accuracy here, at 2.8x the instructions; 191 tokens need 13 B              |
| `jhu-clsp/mmBERT-small`       | 140 M      | multilingual (the base of Julia-1); not measured for this task                      |

13 M of Ettin-17M's parameters are its token-embedding table, which costs almost nothing
per token. Measure a larger model before you rely on it (step 8).

## 2. Design the outputs: one head, a group per fact

llama.cpp runs **one** classification head and returns all of its scores. To read
several facts from one message in one pass, give the head one group of scores per fact
(`heads.py`):

```python
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
```

- **One of several values** (kind, action), or **a level** (toxicity): one score per
  value, softmax over the group.
- **Yes/no** (scam, misleading, respectful): one score, sigmoid.
- **Names:** a score of a group is named `fact:value` (`kind:troll`, `toxicity:2`), and a
  yes/no score is named after its fact alone (`scam`). The names become the model's
  `id2label`, the GGUF's labels, and the `labels` that `run_classify` returns, in order,
  so a caller can check the order it reads.
- **The labels are required:** `load_model` loads a model as a classifier only when its
  GGUF has them. The converter writes them from `id2label`, but drops the default
  `LABEL_0`/`LABEL_1` of a 2-label model, so always set real names (`train.py` does).
- **The loss:** `heads.loss` trains each group as what it is: cross-entropy per softmax
  group, binary cross-entropy per flag. The standard `problem_type` loss of
  `transformers` treats all scores as one softmax, or all as independent sigmoids.

## 3. Label the data

One JSON object per line, the text and every fact:

```json
{"text": "Hi, I'm from the team support desk. DM me your issue and I'll fix your stuck stake directly 🙏", "kind": "spam", "toxicity": 0, "scam": true, "misleading": true, "respectful": false, "action": "block"}
```

What worked, in this order:

1. **Written guidelines with a policy.** Define every value with examples, and write down
   how the facts combine into the subjective ones: for example, every scam is
   `action:block`; toxicity 2 is `block`; a troll's false claim is `delete`; a good-faith
   mistake is `warn`. Without the policy, two labelers gave the same message different
   actions all the time. The guidelines are in the dataset (`GUIDELINES.md`).
2. **Generate, then label again blind.** Three writers each produced 800 messages with
   their labels, from three angles: newcomers and support; governance debate; trolls,
   spam and scams with look-alike contrast messages. About a quarter are hard cases. A
   second set of labelers then labeled every text again, seeing only the text and the
   guidelines. We used LLM agents for both; with people it works the same.
3. **Keep what both passes agree on** (`merge_labels.py`): the same kind, scam and
   misleading, and toxicity within one level. The kept message gets the second pass's
   labels, so the policy facts are consistent everywhere. 3,381 of 3,584 messages passed.
4. **A held-out test set, never trained on.** 200 messages written separately, labeled
   twice; the few disagreements settled by hand. `merge_labels.py` removes every training
   text that is also in the test set. With 100 test messages, a result moved by about ±5
   by chance; 200 makes comparisons more reliable.
5. **Look at the errors, then add targeted data.** See step 5.

## 4. Fine-tune

```bash
make train          # python train.py --base jhu-clsp/ettin-encoder-17m --data data/train.jsonl --out build/model
```

`train.py` holds out 10% for validation, trains 8 epochs (AdamW, learning rate 1e-4,
batch 32, warmup, gradient clipping), and saves the epoch with the best validation loss,
in f32, with the labels in `id2label`.

The validation loss is lowest after 2-3 epochs: on a few thousand examples the model
overfits soon, and keeping the best epoch handles that.

## 5. Evaluate, and improve

```bash
make eval           # python eval.py build/model data/test.jsonl --misses
```

```
kind 0.89 toxicity 0.89 scam 0.99 misleading 0.90 respectful 0.92 action 0.83
all facts right: 141/200
```

`eval.py` scores each fact, counts the messages that get every fact right, and with
`--misses` lists the messages with a wrong fact. How the example got there, on the first
100 test messages:

| Step                                                              | All 6 facts right |
|-------------------------------------------------------------------|-------------------|
| First model: 6 kinds, 2,194 messages, learning rate 5e-5          | 60/100            |
| Learning rate 1e-4                                                | 66/100            |
| Merge kinds `feedback` into `discussion` (a fuzzy boundary)       | 74/100            |
| ~1,100 targeted messages (warn/delete cases, kind boundaries)     | 80/100            |

What did not help: Ettin-32M (the same accuracy at 2.8x the cost), a lower learning rate,
and weighting the action loss by class (within the noise).

Lessons:

- **Merge values that labelers cannot tell apart.** `feedback` vs `discussion` was the
  largest confusion of the model, and of the two labeling passes. Merging them gained
  more than any other change.
- **Read the confusions, then write data for them.** The first model let mild mockery
  and good-faith mistakes through with `none`; examples written for exactly those cases
  improved them.
- **Training runs vary.** Two seeds of the same data gave 133 and 141 of 200. Choose by
  validation loss, not by the test set.
- **For moderation, look at the actions, not only the total:** of the test messages that
  deserve a block (scams, abuse), how many are blocked; of the legitimate ones, how many
  are wrongly deleted or blocked.

## 6. Convert to GGUF, f32

```bash
make gguf           # convert_hf_to_gguf.py build/model --outtype f32 --outfile build/dao-moderation-f32.gguf
```

It uses `convert_hf_to_gguf.py` from the fork in this repo (`src/llama_cpp_onicai_fork`),
which needs `torch`, `transformers` and `numpy`, as the training environment has. The
release zip has no llama.cpp: there, pass a llama.cpp checkout, e.g.
`make gguf LLAMA_CPP=~/llama.cpp` (upstream's converter writes the same GGUF).

**Use `--outtype f32`, not f16.** In wasm, f32 runs on SIMD, while f16 converts every
weight one by one as it reads it: an f32 model is 3.6x cheaper per token, with the same
scores (see [README-classifier-models.md](README-classifier-models.md)).

The GGUF holds:
- the labels, as `<arch>.classifier.output_labels`;
- the pooling, as `<arch>.classifier.pooling_type`, from `classifier_pooling` in
  `config.json`;
- the head tensors `cls.*`.

## 7. Check the GGUF against PyTorch

Build `llama-embedding` from the fork once, outside the source tree, CPU only like the
canister. From the repository root (from the release zip, use the llama.cpp checkout you
passed as `LLAMA_CPP` in place of `src/llama_cpp_onicai_fork`):

```bash
cmake -S src/llama_cpp_onicai_fork -B build-llama -DCMAKE_BUILD_TYPE=Release -DLLAMA_CURL=OFF -DGGML_METAL=OFF
```

```bash
cmake --build build-llama --target llama-embedding llama-quantize -j 8
```

This is what `run_classify` computes on the canister:

```bash
build-llama/bin/llama-embedding -m examples/classifier/build/dao-moderation-f32.gguf --pooling rank --embd-normalize -1 -p "gm"
```

Write its 15 scores for every test message as one JSON list per line, and score them with
the same evaluation, from `examples/classifier`:

```bash
python eval.py --scores build/llama-scores.jsonl data/test.jsonl
```

For the example, llama.cpp gives the same 141/200 as PyTorch, with every score within
0.016. A larger difference means the conversion went wrong: check that the labels are in
the right order, and that `classifier_pooling` in `config.json` matches the pooling the
model was trained with.

## 8. Deploy and check on the canister

Upload the GGUF and load it, as in [README-classifier-models.md](README-classifier-models.md).
From the repository root:

```bash
python -m scripts.upload --network local --canister llama_cpp --canister-filename models/model.gguf examples/classifier/build/dao-moderation-f32.gguf
```

```bash
icp canister call llama_cpp load_model '(record { args = vec {"--model"; "models/model.gguf"; "--embedding"; "--pooling"; "rank"; "--no-warmup"; "-c"; "192"; "-ub"; "192"} })' -e local
```

Set `-c` and `-ub` to the longest text you serve, in tokens with the special tokens, and
check the cost:

- `run_classify` returns `input_tokens` and `instructions` for every call;
- a text that fills `-ub` exactly is the most expensive one, and it must fit the
  **5 B instructions** of `run_classify_query` (or the 40 B of `run_classify`);
- for Ettin-17M in f32 that is `-ub 192`, at ~25 M instructions per token.

Then run the whole test set through `run_classify_query`, write the scores as in step 7,
and score them with `eval.py --scores`: the acceptance test. The example's canister gets
the same 141/200, its scores within 0.013 of llama.cpp on the CPU, at 0.07-1.67 B
instructions per message.

## 9. Read the scores in the caller

`run_classify` returns the raw scores, in the order of `labels`. Apply what the model was
trained with: softmax over each group and take the top value; sigmoid for each flag.
That is `facts()` in `heads.py`; in a bot written in another language, port those few
lines and keep the `SLICES` layout in one place.

Check `labels` against the layout once, when the caller starts or after a model change.
Then a model trained with a different layout fails loudly instead of being read wrongly.

**An OpenChat moderation bot calls `run_classify_query`:** it runs off-chain, so the query
answers in one round trip and costs it no cycles. See
[README-classifier-models.md](README-classifier-models.md#built-for-an-openchat-moderation-bot-use-run_classify_query).
