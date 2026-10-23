# Sequence classifiers (BERT / ModernBERT)

> A **sequence classifier** reads one text and returns one raw score per label from
> its classification head, in one forward pass, without generating text. Use one
> when you need a few fixed facts about a short text (intent, tone, a yes/no flag)
> cheaply and in one call.
>
> It runs on the **same `llama_cpp` canister** as the chat models. Follow the main
> [README.md](../README.md) for `# Set up` (icp-cli, Python env, build, deploy, cycles),
> then use the steps here.

This is what upstream llama.cpp does with
`llama-embedding -m <gguf> --pooling rank -p "<text>" --embd-normalize -1`, or
`llama-server --embedding --pooling rank`. The canister takes the same flags. Any
BERT-family GGUF with a classification head works: `llama.cpp`'s
`convert_hf_to_gguf.py` converts a `...ForSequenceClassification` model, and writes
its labels into the GGUF (`<arch>.classifier.output_labels`).

To train your own, see [README-classifier-training.md](README-classifier-training.md).

## The example model: DAO-channel moderation

[onicai/dao-moderation-classifier](https://huggingface.co/onicai/dao-moderation-classifier)
reads one chat message from the public channel of a DAO and scores 6 facts at once, as
15 scores of one classification head:

| Scores | Labels                                                                       | Read as                       |
|--------|------------------------------------------------------------------------------|-------------------------------|
| 0-4    | `kind:question` `kind:discussion` `kind:support` `kind:troll` `kind:spam`    | softmax: the kind of message  |
| 5-7    | `toxicity:0` `toxicity:1` `toxicity:2` (civil, rude, abusive)                | softmax: the level            |
| 8      | `scam`                                                                       | sigmoid: P(scam)              |
| 9      | `misleading`                                                                 | sigmoid: P(misleading)        |
| 10     | `respectful`                                                                 | sigmoid: P(respectful)        |
| 11-14  | `action:none` `action:warn` `action:delete` `action:block`                   | softmax: the moderator action |

It is ModernBERT Ettin-encoder-17M (7 layers x 256), fine-tuned in f32 on 3,381 synthetic
messages labeled twice under one written moderation policy. How it was made, and how to
train your own: [README-classifier-training.md](README-classifier-training.md).

## Built for an OpenChat moderation bot: use `run_classify_query`

**For moderating an OpenChat channel, call `run_classify_query`.** A bot that watches an
OpenChat channel runs off-chain and calls the canister directly, so it can use the query:

- it is answered in one round trip, without waiting for consensus;
- it costs the caller no cycles;
- a message of up to 192 tokens fits the query's 5 B instructions (see below), and a chat
  message is far shorter (the 200 test messages are 3-55 tokens, 0.61 B on average).

A query answer comes from one replica and is not certified (see
[Query or update?](#query-or-update)). That fits moderation: the worst case is one message
misjudged, and a moderator action can be reversed. Use `run_classify`, an update, when
another canister calls the classifier, or when a decision must hold against a faulty
replica.

## Results (local replica, llama_cpp_canister with llama.cpp b11476)

On 200 held-out test messages, through `run_classify_query` on the canister:

| kind | toxicity | scam | misleading | respectful | action | all 6 facts right |
|------|----------|------|------------|------------|--------|-------------------|
| 0.89 | 0.89     | 0.99 | 0.90       | 0.92       | 0.83   | 141/200           |

- PyTorch, llama.cpp on the CPU and the canister give the same 141/200: their scores
  differ by at most 0.016.
- The weakest spot is the middle of the action scale: a message that deserves a warn
  (mild mockery, a good-faith mistake) often gets none. Scams and abuse are blocked.

### How a bot should act on it: only when confident

"All 6 facts right" counts a message as wrong when any fact is off, also one a bot does
not act on. For moderation, what matters is the action, and how sure the model is of it:
the softmax probability of the top action (`action_p` in `facts()` of
`examples/classifier/heads.py`). On the 200 test messages, of which 99 need an action:

| The bot acts when the probability is at least | Acts on | Good messages hit | Wrong blocks | Real blocks caught |
|-----------------------------------------------|---------|-------------------|--------------|--------------------|
| any (always acts)                             | 75      | 2                 | 2            | 40 of 46           |
| 0.6                                           | 67      | 2                 | 2            | 36 of 46           |
| **0.8**                                       | **54**  | **0**             | **0**        | **34 of 46**       |
| 0.9                                           | 36      | 0                 | 0            | 25 of 46           |

**Recommended: act automatically when the probability is at least 0.8, and queue
everything else for a human moderator.** At 0.8 the bot handled 54 of the 99 messages that need an action without a
single wrong action, and left the hard cases to a person. Keep a human in the loop for
`block` on a real channel.

- The 2 wrong blocks below 0.8 were good messages close to a scam: a member warning about
  a phishing site, and a question about recovering an account.
- The model missed 3 messages that deserve a block: a pump-group invite, a copypasta, and
  a veiled threat ("if i ever meet @someone irl he will regret it"). Threats in
  particular need a human, or more training data like them.
- The model was trained on synthetic messages. Measure it on messages from your own
  channel before you let it act, and add labeled messages from that channel to the
  training data ([README-classifier-training.md](README-classifier-training.md)).

## Which file: f32

**Use f32.** f32 runs on wasm SIMD; f16 does not (each f16 weight is converted to f32
one by one as it is read), so f32 is 3.6x cheaper than f16. All four files answer the
test set the same:

| Measure                              | **f32**                             | f32 + Q8_0 embd                | Q8_0                           | f16                          |
|--------------------------------------|-------------------------------------|--------------------------------|--------------------------------|------------------------------|
| file                                 | **69 MB**                           | 31 MB                          | 20 MB                          | 36 MB                        |
| instructions per token               | **~25 M**                           | ~25 M                          | ~19 M                          | ~89 M                        |
| a 33-token message                   | **0.88 B**                          | 0.88 B                         | 0.62 B                         | ~2.9 B                       |
| longest text in a query (5 B)        | **192 tokens = 4.5 B** (`-ub 192`)  | 192 tokens = 4.5 B (`-ub 192`) | 256 tokens ≈ 4.2 B (`-ub 256`) | 48 tokens ≈ 4.4 B (`-ub 48`) |
| longest text in an update (40 B)     | **512 tokens = 13.7 B** (`-ub 512`) |                                | 512 tokens ≈ 9.7 B (`-ub 512`) | ~447 tokens = 39.6 B (`-ub 512`) |
| `wasm_heap_bytes` after `load_model` | **108 MB** (`-ub 192`)              | 70 MB (`-ub 192`)              | 59 MB (`-ub 256`)              | 72 MB (`-ub 48`)             |
| all 6 facts right (200 messages)     | **141**                             | 141                            | 143                            | 141                          |
| largest score difference vs f32      |                                     | 0.15                           | 0.57                           | 0.026                        |

The longest text is `-ub` tokens, special tokens included. ≈: measured one token shorter.

- f32 + Q8_0 embd quantizes only the token-embedding table (13 of the 17 M parameters,
  used only as a lookup): less than half the file and about 35% less heap than f32, at
  the same cost per token.
- A full Q8_0 also quantizes the inputs of every matrix multiplication: the cheapest per
  token, and the scores move most, although the facts did not change on this test set.
  Check a smaller variant on your own test set before you use it.
- The instructions and the heap depend on the architecture, the file format and `-ub`,
  not on the fine-tuning: another classifier fine-tuned from Ettin-encoder-17M costs the
  same in the same format and with the same load arguments.

## Get the gguf

The f32 file is on Hugging Face:
[onicai/dao-moderation-classifier](https://huggingface.co/onicai/dao-moderation-classifier).

```bash
mkdir -p models/onicai/dao-moderation-classifier
```

```bash
curl -L -C - -o models/onicai/dao-moderation-classifier/dao-moderation-f32.gguf https://huggingface.co/onicai/dao-moderation-classifier/resolve/main/dao-moderation-f32.gguf
```

Verify the sha256:

```bash
shasum -a 256 models/onicai/dao-moderation-classifier/dao-moderation-f32.gguf
# -> 47856638d128afc076244ba6df67a59dfaf6b20de24de39f198720c7daa400e4
```

## Upload the gguf

```bash
python -m scripts.upload --network local --canister llama_cpp --canister-filename models/model.gguf --filetype gguf --hf-sha256 47856638d128afc076244ba6df67a59dfaf6b20de24de39f198720c7daa400e4 models/onicai/dao-moderation-classifier/dao-moderation-f32.gguf
```

The commands on this page run as the identity that deployed the canister (the default
one of [README.md](../README.md)'s `# Set up`): the endpoints are controller-only by
default. To use another identity, add `--identity <name>` to the `icp` and pytest
commands, and set `ICPP_PRO_TEST_IDENTITY=<name>` for `python -m scripts.upload`, which
has no `--identity` option.

## Load the model

```bash
icp canister call llama_cpp load_model '(record { args = vec {"--model"; "models/model.gguf"; "--embedding"; "--pooling"; "rank"; "--no-warmup"; "-c"; "192"; "-ub"; "192"} })' -e local
```

- `--embedding --pooling rank`: as upstream, and only together. Without them the model
  loads, but as no classifier: `run_classify` returns an error, and so does generation
  ("The loaded model cannot generate text"). With them,
  `load_model` refuses a model that has no classifier labels
  (`<arch>.classifier.output_labels`): llama.cpp counts the outputs of the head from
  them, and without them it cannot tell a classifier from any other model.
  `convert_hf_to_gguf.py` writes them from `id2label` in `config.json`. It skips the
  default `LABEL_0`/`LABEL_1` of a 2-label model, so give such a model real label names
  (see [README-classifier-training.md](README-classifier-training.md)).
- `-ub` (and `-c`) is the **longest text in tokens**, special tokens included. A
  classifier is a bidirectional encoder, so the whole text is decoded in one batch, and
  one text cannot be split over several messages. Pick it from the table above:
  - **f32: `-c 192 -ub 192`**, so that every text fits a query (4.5 B at most);
  - f16: `-c 48 -ub 48`; Q8_0: `-c 256 -ub 256`.
  - For texts longer than the query limit allows, load with a larger `-c` and `-ub`
    (f32: up to `-c 512 -ub 512`, 13.7 B for a 512-token text) and use `run_classify`:
    a text longer than the loaded `-ub` gets the token-limit error.
- `-b` follows `-ub` by itself.

## Classify a text

As a query:

```bash
icp canister call llama_cpp run_classify_query '(record { text = "Hi, I am from the team support desk. DM me your issue and I will fix your stuck stake directly" })' -e local --query
```

```
(variant { Ok = record {
  scores = vec { 1.676; -1.285; -3.225; -1.565; 11.258; ...; 7.266 }; // 15 raw scores in label order (rounded here)
  labels = vec { "kind:question"; "kind:discussion"; ...; "action:block" };
  instructions = opt (603_853_217 : nat64);                           // what this call used
  input_tokens = 24 : nat64;                                          // with the special tokens
} })
```

Read as: kind spam, scam, misleading, not respectful, action block.

`run_classify` takes the same input and gives the same reply, as an update. The
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
| An OpenChat moderation bot                      | `run_classify_query` | one round trip, no cycles; a misjudged message can be reversed |
| A frontend that needs the answer fast           | `run_classify_query` | one round trip; fits when `-ub` is set as above  |
| Another canister calling it                     | `run_classify`       | a call from a canister runs as an update anyway  |
| An answer that must be trusted                  | `run_classify`       | a query answer comes from one replica, uncertified |
| Texts longer than the query limit allows        | `run_classify`       | f32: 512 tokens = 13.7 B, far below 40 B         |

**How far can a query answer be trusted?** A query runs on one replica of the subnet,
without consensus. The replica signs its response with its node key, and the agent
libraries (agent-js and the Rust `ic-agent`, which icp-cli uses) check that signature
against the subnet's node keys by default. That proves the answer came from a node of
the canister's subnet, unchanged by a boundary node or the network, but not that the
other nodes agree: one faulty or malicious replica can still return a wrong answer.
Certified data does not help here either, because it certifies state set in an update,
not a computation on the caller's text. When the answer must hold against a malicious
node, call `run_classify`: an update goes through consensus and its reply is certified
by the subnet.

## Limits and errors

- **A text with more tokens than `-ub`** (or `-c`) gets
  `Err "text is N tokens, more than the M of the loaded context (-c, -b, -ub of load_model)"`.
  The check runs before the decode and costs almost nothing.
- **An empty text** and **a text over 8192 bytes** get an `Err` too.
- **Over the instruction limit:** with a larger `-ub` than the table gives, a long text can
  still need more instructions than the call allows: 5 B for the query, 40 B for the
  update (f16 at `-ub 512`: a text over ~447 tokens; f32 needs 13.7 B for 512
  tokens). The IC then traps the message ("Canister exceeded the limit of ... instructions",
  IC0522), all its changes are rolled back, and
  the caller gets a reject, never a partial result. There is no `run_classify_full`: one text cannot be split over messages.
- **No generation:** while a classifier is loaded, `new_chat`, `run_update`, `run_query`
  and `run_update_full` return `The loaded model is a sequence classifier: use
  run_classify.`, and `run_decision` returns that the model is not a decision model.

## Tests

- Native, in the repo: `native/test_classify.cpp` (skipped when the gguf is not downloaded).
- pytest, after uploading (it loads the model itself, with the documented arguments):

```bash
pytest -vv --network local --identity llama-cpp-testing test/test_classify.py
```

It checks three reference messages against llama.cpp's CPU scores within 0.05, the query
against the update, the errors, the access and the generation guard.

## Appendix: the files we measured

| File                                 | Size             | sha256                                                             | Where                                     |
|--------------------------------------|------------------|--------------------------------------------------------------------|-------------------------------------------|
| `dao-moderation-f32.gguf`            | 69,242,400 bytes | `47856638d128afc076244ba6df67a59dfaf6b20de24de39f198720c7daa400e4` | Hugging Face, onicai/dao-moderation-classifier |
| `dao-moderation-f32-q8embd.gguf`     | 31,365,664 bytes | `e7d1561a78e4d541e713249fc82dab9824152d6f18b6a5916d94f74e2a6148f1` | not published; made for this comparison   |
| `dao-moderation-q8_0.gguf`           | 19,707,424 bytes | `cf9ee100f9b8a1eedca603d571cb8b0ef568235f2fe61bd33e5e09f4851136f3` | not published; made for this comparison   |
| `dao-moderation-f16.gguf`            | 35,516,448 bytes | `118ee02607b08d68b359b240554af2205ef046c4f0e4078ffd0147d0f9823430` | not published; made for this comparison   |

All four come from the same fine-tuned f32 model (`examples/classifier`, `make gguf` for
the f32 file):

```bash
python src/llama_cpp_onicai_fork/convert_hf_to_gguf.py build/model --outtype f32 --outfile dao-moderation-f32.gguf
python src/llama_cpp_onicai_fork/convert_hf_to_gguf.py build/model --outtype f16 --outfile dao-moderation-f16.gguf
llama-quantize --tensor-type blk=f32 --tensor-type cls=f32 dao-moderation-f32.gguf dao-moderation-f32-q8embd.gguf Q8_0
llama-quantize dao-moderation-f32.gguf dao-moderation-q8_0.gguf Q8_0
```
