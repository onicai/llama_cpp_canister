# Sequence classifiers (BERT / ModernBERT)

> A **sequence classifier** reads one text and returns one raw score per label from
> its classification head, in one forward pass, without generating text. Use one
> when you need a few fixed facts about a short text (intent, tone, a yes/no flag)
> cheaply and in one call.
>
> It runs on the **same `llama_cpp` canister** as the chat models. Follow the main
> [README.md](README.md) for `# Set up` (icp-cli, Python env, build, deploy, cycles),
> then use the steps here.

This is what upstream llama.cpp does with
`llama-embedding -m <gguf> --pooling rank -p "<text>" --embd-normalize -1`, or
`llama-server --embedding --pooling rank`. The canister takes the same flags. Any
BERT-family GGUF with a classification head works: `llama.cpp`'s
`convert_hf_to_gguf.py` converts a `...ForSequenceClassification` model, and writes
its labels into the GGUF (`<arch>.classifier.output_labels`).

## The model we tested: Ettin-encoder-17M

The guard model of treasure-guard (an onicai game), a fine-tuned
`ModernBertForSequenceClassification` (Ettin-encoder-17M, 7 layers x 256, mean
pooling) with 18 labels: `intent:gift|bribe|threat|trick|plea|chat`,
`respect:0..4`, `humor:0..4`, `icp` and `steal`.

**Use f32.** f32 runs on wasm SIMD; f16 does not (each f16 weight is converted to f32
one by one as it is read), so f32 is 3.6x cheaper than f16, with the same scores.

**Smaller: f32 layers with a Q8_0 embedding table.** The token-embedding table is 13 of the
17 M parameters, and only a lookup: quantizing just that table halves the file and the heap
at the same cost per token, and moves the scores by up to 0.06. A full Q8_0 is the cheapest
per token, but quantizes the inputs of every matrix multiplication too, and moves the scores
by up to 0.54.

| File                                 | Size             | sha256                                                             |
|--------------------------------------|------------------|--------------------------------------------------------------------|
| `ettin-encoder-17m-f32.gguf`         | 69,245,536 bytes | `131792413b5437b08ce8e12edf1e7fd506bae5ef611bed457d7d8fbdd7c26dc7` |
| `ettin-encoder-17m-f32-q8embd.gguf`  | 31,368,800 bytes | `5664953e0d5f8c9f6c4a5d72653b5f51272403acb22f40fef5ba5f6d633c6630` |
| `ettin-encoder-17m-f16.gguf`         | 35,518,048 bytes | `89b8d56fb6c06c4386e08c36968b651413fd9cdfee51aaf06d02a29c01194312` |
| `ettin-encoder-17m-q8_0.gguf`        | 19,708,288 bytes | `d594728c6468e7b51b2a0ec0b007f50eec072891dd872b7efd61ae4b34804902` |

Made from the f16 file with `llama-quantize` built from the fork:

```bash
llama-quantize ettin-encoder-17m-f16.gguf ettin-encoder-17m-f32.gguf F32
llama-quantize --tensor-type blk=f32 --tensor-type cls=f32 ettin-encoder-17m-f32.gguf ettin-encoder-17m-f32-q8embd.gguf Q8_0
llama-quantize ettin-encoder-17m-f16.gguf ettin-encoder-17m-q8_0.gguf Q8_0
```

(`convert_hf_to_gguf.py --outtype f32` writes f32 directly from the Hugging Face model.)

## Headline results (local replica, llama_cpp_canister with llama.cpp b11476)

| Measure                              | **f32**              | f32 + Q8_0 embd      | f16                 | Q8_0                 |
|--------------------------------------|----------------------|----------------------|---------------------|----------------------|
| instructions per token               | **~25 M**            | ~25 M                | ~89 M               | ~19 M                |
| a 14-19 token text                   | **0.33-0.46 B**      | 0.33-0.46 B          | 1.2-1.65 B          | 0.22-0.31 B          |
| a 138-character message (33 tokens)  | **0.88 B**           | 0.88 B               | ~2.9 B              | 0.61 B               |
| longest text in a query (5 B)        | **191 tokens = 4.5 B** (`-ub 192`) | 191 tokens = 4.5 B (`-ub 192`) | 47 tokens = 4.3 B (`-ub 48`) | 255 tokens = 4.2 B (`-ub 256`) |
| longest text in an update (40 B)     | **511 tokens = 13.7 B** (`-ub 512`) |                     | ~447 tokens = 39.6 B | 511 tokens = 9.7 B  |
| `wasm_heap_bytes` after `load_model` | **108 MB** (`-ub 192`) | 70 MB (`-ub 192`)  |                     |                      |
| largest score difference             | **0.016** vs upstream | 0.061 vs f32        | 0.016 vs upstream   | 0.54 vs f16          |

- f32 and f16 match upstream `llama-embedding` (f16) within 0.016; the native build of the
  fork matches it exactly.
- On the 4 texts we checked, every variant gave the same answer as f16 for every fact (the
  top label of intent, respect and humor, and the sign of icp and steal). Check a smaller
  variant on your own test set before you use it.

## Get the gguf

The f32 file and the f32 + Q8_0-embedding file are on Hugging Face:
[onicai/treasure-guard](https://huggingface.co/onicai/treasure-guard).

```bash
mkdir -p models/onicai/treasure-guard
```

```bash
wget -c -O models/onicai/treasure-guard/ettin-encoder-17m-f32.gguf https://huggingface.co/onicai/treasure-guard/resolve/main/ettin-encoder-17m-f32.gguf
```

Verify the sha256 against the table above:

```bash
shasum -a 256 models/onicai/treasure-guard/ettin-encoder-17m-f32.gguf
```

## Upload the gguf

```bash
python -m scripts.upload --network local --canister llama_cpp --canister-filename models/model.gguf models/onicai/treasure-guard/ettin-encoder-17m-f32.gguf
```

## Load the model

```bash
icp canister call llama_cpp load_model '(record { args = vec {"--model"; "models/model.gguf"; "--embedding"; "--pooling"; "rank"; "--no-warmup"; "-c"; "192"; "-ub"; "192"} })' -e local
```

- `--embedding --pooling rank`: as upstream. Without them the model loads, but as no
  classifier, and `run_classify` returns an error.
- `-ub` (and `-c`) is the **longest text in tokens**, special tokens included. A
  classifier is a bidirectional encoder, so the whole text is decoded in one batch, and
  one text cannot be split over several messages. Pick it from the table above:
  - **f32: `-c 192 -ub 192`**, so that every text fits a query (4.5 B at most);
  - f16: `-c 48 -ub 48`; Q8_0: `-c 256 -ub 256`.
- `-b` follows `-ub` by itself.

## Classify a text

```bash
icp canister call llama_cpp run_classify '(record { text = "I grab a handful of coins from the chest and run!" })' -e local
```

```
(variant { Ok = record {
  scores = vec { -1.152; 2.170; -0.543; ...; 3.071 };    // 18 raw scores, in label order
  labels = vec { "intent:gift"; "intent:bribe"; ...; "steal" };
  input_tokens = 14 : nat64;                             // with the special tokens
  instructions = opt (331_505_759 : nat64);              // what this call used
} })
```

`run_classify_query` takes the same input and gives the same reply, as a query. The
scores are the raw outputs of the head, as upstream returns them with
`--embd-normalize -1`: turn them into probabilities as the model was trained to.

Candid:

```candid
type ClassifyInputRecord = record { text : text };
type ClassifyOutputRecord = record {
  scores : vec float64;
  labels : vec text;
  input_tokens : nat64;
  instructions : opt nat64;
};
run_classify : (ClassifyInputRecord) -> (variant { Ok : ClassifyOutputRecord; Err : ApiError });
run_classify_query : (ClassifyInputRecord) -> (variant { Ok : ClassifyOutputRecord; Err : ApiError }) query;
```

Access is the same as for `run_decision`: controllers, the `AdminUpdate` role
(`AdminQuery` for the query), or any caller that is not anonymous at access level 1.

## Query or update?

| Use case                                        | Call                 | Why                                              |
|-------------------------------------------------|----------------------|--------------------------------------------------|
| A frontend that needs the answer fast           | `run_classify_query` | one round trip; fits when `-ub` is set as above  |
| Another canister calling it                     | `run_classify`       | a call from a canister runs as an update anyway  |
| An answer that must be trusted                  | `run_classify`       | a query answer comes from one replica, uncertified |
| Texts longer than the query limit allows        | `run_classify`       | f32: 511 tokens = 13.7 B, far below 40 B         |

## Limits and errors

- **A text with more tokens than `-ub`** (or `-c`) gets
  `Err "text is N tokens, more than the M of the loaded context (-c, -b, -ub of load_model)"`.
  The check runs before the decode and costs almost nothing.
- **An empty text** and **a text over 8192 bytes** get an `Err` too.
- **Over the instruction limit:** with a larger `-ub` than the table gives, a long text can
  still need more instructions than the call allows: 5 B for the query, 40 B for the
  update (f16 at `-ub 512`: a text over ~447 tokens; f32 needs 13.7 B for 511 tokens). The IC then traps the message
  (IC0522), all its changes are rolled back, and the caller gets a reject, never a partial
  result. There is no `run_classify_full`: one text cannot be split over messages.
- **No generation:** while a classifier is loaded, `new_chat`, `run_update`, `run_query`
  and `run_update_full` return `The loaded model is a sequence classifier: use
  run_classify.`, and `run_decision` returns that the model is not a decision model.

## Tests

- Native: `native/test_classify.cpp` (skipped when the gguf is not downloaded).
- pytest, after uploading and before loading:

```bash
pytest -vv --network local --identity llama-cpp-testing test/test_classify.py
```

It checks the three reference texts against upstream's scores within 0.05, the query
against the update, the errors, the access and the generation guard.
