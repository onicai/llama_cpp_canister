# Hindi on llama_cpp_canister — which model to use

Hindi is the hardest language this canister serves, and for two reasons that are
easy to confuse with each other:

1. **Tokenizer efficiency** decides how many `run_update` calls a Hindi answer
   costs. Devanagari is 3 bytes per character in UTF-8, and a model whose
   tokenizer has no Devanagari vocabulary falls back to byte tokens — so the same
   sentence costs 4-6× the tokens it would in English. More tokens means more
   calls, a faster-growing conversation, and a per-call cost that eventually
   crosses the 40 B instruction limit (`IC0522`).
2. **Training data** decides whether the Hindi is any good. This is independent
   of (1): a model can tokenize Devanagari beautifully and still write nonsense.

The models this repo shipped with fail on **both** counts. Two 1 B models fix
both.

**Verdict: use [Gemma-3-1B-it](https://huggingface.co/google/gemma-3-1b-it) for
Hindi.** It is the only model measured here that writes correct, idiomatic Hindi
*and* tokenizes it efficiently, and it does so at 1 B parameters.

---

## Headline: measured tokenizer efficiency

Token counts for the exact IConfucius Hindi turn (each model's own chat template)
and for a 20-word Hindi aphorism (template-free, so this column is the pure
fertility signal).

| model                     | HI prompt | HI quote | tok/word | vs Gemma-3 |
| ------------------------- | --------: | -------: | -------: | ---------: |
| **Gemma-3-1B-it**         |    **66** |   **27** | **1.35** |         1× |
| LilMoo-v0.2 *(base only)* |        71 |       26 |     1.30 |      0.96× |
| sarvam-1 *(base only)*    |        90 |       28 |     1.40 |      1.04× |
| Llama-3.2-1B-Instruct     |       101 |       51 |     2.55 |      1.9× |
| Qwen3-0.6B / Qwen3-1.7B   |       185 |       92 |     4.60 |      3.4× |
| LFM2.5-1.2B-Instruct      |       243 |      123 |     6.15 |      4.6× |

For scale, the same 20-word idea in **English** costs every one of these models
24-26 tokens. So Gemma-3 pays a ~1.1× Hindi tax, while LFM2.5 pays ~5×.

These numbers are not from a paper — they are measured from each model's own
`tokenizer.json`, and they are confirmed against the canister itself: the
canister reports 242 prompt tokens plus BOS for the LFM2.5 Hindi turn (243 here)
and 181-185 for Qwen3 (185 here).

**Qwen3-1.7B shares the Qwen3-0.6B tokenizer exactly.** Moving up that family
buys no Hindi efficiency at all while lowering the token ceiling.

## Headline: measured generation quality

Ten IConfucius topics, the real Hindi system and user prompt, `--temp 0.7`, fixed
seeds, run natively against the same fork build that is compiled into the wasm
(`build 10078 (6bd774370)`), so the output matches what the canister produces.

| model                     | non-Hindi forms | terminated cleanly | mean output tokens | usable? |
| ------------------------- | --------------: | -----------------: | -----------------: | ------- |
| **Gemma-3-1B-it**         |        **0/10** |          **10/10** |             **23** | **yes** |
| Llama-3.2-1B-Instruct     |            0/10 |               8/10 |                 84 | mostly ‡ |
| LFM2.5-1.2B-Instruct      |            5/10 |               3/10 |                125 | no      |
| Qwen3-0.6B                |            0/10 |               0/10 |             ≥140 † | no      |

† Qwen3-0.6B hit the 140-token cap on all ten topics — it never emits EOS.
‡ Llama-3.2-1B's Hindi is correct, but it did not terminate at all on the
canister at greedy decoding — see the call-cost section below.

**Read the "non-Hindi forms" column carefully.** It counts one specific failure —
Nepali-ish oblique forms (`यसली`, `यसको`, `यसलिए` where Hindi wants `इसलिए`,
`इसको`, `इस`) and the invented `सच्छा`. That is LFM2.5's signature, so its 5/10 is
meaningful. Qwen3-0.6B scores 0/10 on this metric and is still unusable: its
failure mode is different — grammatical-looking word salad that never terminates.
No single automated metric catches both, which is why the samples below matter.

### Samples

**Gemma-3-1B-it** — real aphorisms, concise, always terminates:

> ज्ञान: "अज्ञानता ही भ्रम का आधार है, और ज्ञान ही भ्रम का समाधान।"
> *(Ignorance is the foundation of illusion, and knowledge is its resolution.)*

> मित्रता: "समानता से उत्पन्न, भिन्नता से समझी, और परोपकार से परिपूर्ण।"
> *(Born of sameness, understood through difference, fulfilled in generosity.)*

**Llama-3.2-1B-Instruct** — grammatical and clearly Hindi, but verbose and often
drifts away from the aphorism form; 2/10 ran to the token cap:

> समय: "समय, आपकी गहराई से आपने नहीं ली है, लेकिन आपकी छाया में आपने मेरी खुदाई ली है।"

**LFM2.5-1.2B-Instruct** — largely invented vocabulary, instruction leakage, one
outright refusal:

> धैर्य: "धैर्य, यस सूक्ति पर महत्वपूर्ण है। यह स्वयापूर्ण प्रतिक्रियास्था है। यसको विसंधान करना सूते होता है…"

**Qwen3-0.6B** — echoes the instruction as a heading, then degenerates:

> मित्रता: "**मित्रता आदेश और सूक्ति:** मित्रता जी जाती है और जो दुख जीवित नहीं सकता है, उनकी जान जो जाती है और जान जो जाती है और उनकी जान जो जाती है…"

Every model writes good **English** on the identical harness, including LFM2.5
("Patience is the quiet force that turns the storm into stillness"). The failure
is the language, not the prompt, the template, or the build.

## What this costs on the canister

`run_update` processes at most `max_tokens_update` tokens per call, for both
prompt ingestion and generation, so:

```
calls per quote  =  ceil(prompt_tokens / max_tokens)  +  ceil(output_tokens / max_tokens)
```

This formula is exact — measured LFM2.5 ingestion took 31, 27 and 25 calls at
`max_tokens` 8, 9 and 10, against `ceil(243/8) = 31`, `ceil(243/9) = 27` and
`ceil(243/10) = 25`.

All rows below are **measured on the canister**, at each model's production
`max_tokens` (one below its measured ceiling):

| model                 | max_tokens | ingest calls | generate calls | **calls per Hindi quote** |
| --------------------- | ---------: | -----------: | -------------: | ------------------------: |
| **Gemma-3-1B-it**     |     **10** |        **7** |          **4** |                    **11** |
| Llama-3.2-1B-Instruct |          7 |           15 |  did not stop  |        ≥96, never finishes |
| Qwen3-0.6B            |         20 |           10 |             ≥7 |      ≥17, never finishes  |
| LFM2.5-1.2B-Instruct  |          8 |           31 |             16 |                        47 |

**Gemma-3-1B needs ~4× fewer calls per Hindi quote than LFM2.5** — and unlike the
other three it actually finishes. Its Hindi quote completes in 11 calls; the same
quote costs LFM2.5 47, and neither Llama-3.2 nor Qwen3 terminates at all.

Llama-3.2-1B deserves a note: it writes *correct* Hindi (see the quality table),
but on the canister at greedy decoding it fell into a repetition loop and ran
**81 generation calls without emitting EOG** before tripping an `IC0502`. Its
native run at `--temp 0.7` terminated 8/10, so this is a robustness gap rather
than a flat failure — but it is a real risk for an unattended service.

## max_tokens: the ceiling is the same for Hindi as for English

Measured on a local replica, which reproduces the documented mainnet ceiling for
this model class exactly (LFM2.5 English: 9 ok, 10 → `IC0522`):

| model                 | language | ceiling | evidence                                                          |
| --------------------- | -------- | ------: | ----------------------------------------------------------------- |
| **Gemma-3-1B-it**     | English  |  **11** | 11 ok (4 ingest + 4 generate); 12 → `IC0522` on first generate     |
| **Gemma-3-1B-it**     | Hindi    |  **11** | 11 ok (6 ingest + 4 generate); 12 → `IC0522` on first generate     |
| Llama-3.2-1B-Instruct | Hindi    |       8 | 9 → `IC0522` on first generate; 12 → `IC0522` during ingest        |
| LFM2.5-1.2B-Instruct  | English  |       9 | 9 ok (5 ingest + 3 generate); 10 → `IC0522` on first generate      |
| LFM2.5-1.2B-Instruct  | Hindi    |       9 | 9 ok (27 ingest + 36 generate, no trap); 10 → `IC0522` on generate |

**Language does not lower the instruction-limit ceiling** — for either model the
Hindi and English ceilings are identical. Gemma-3-1B's ceiling is *higher* than
LFM2.5-1.2B's despite comparable size, so use **10**.

**Language does not lower the instruction-limit ceiling.** The ceiling is set by
model size and context length. What Hindi changes is the *number of calls*, via
the tokenizer — which is exactly the table above.

This corrects a natural but wrong assumption: the IConfucius Hindi failures were
not Hindi hitting a lower per-call ceiling. They were Hindi needing so many more
calls that the conversation grew long enough for the per-call cost to cross the
limit. See `README.md` Appendix A on per-call cost growing with conversation
length.

## Recommendation

Use **Gemma-3-1B-it at `max_tokens_update = 10`** for Hindi (ceiling 11, one
below as everywhere else in this repo).

| fact                        | value                                                              |
| --------------------------- | ------------------------------------------------------------------ |
| gguf (Q4_K_M)               | 806,058,240 B                                                        |
| gguf sha256                 | `8ccc5cd1f1b3602548715ae25a66ed73fd5dc68a210412eea643eb20eb75a135`   |
| wasm heap after load        | 911,736,832 B (869 MiB)                                              |
| load args                   | `-c 4096 --batch-size 8 --ubatch-size 8 --cache-type-k q8_0 --cache-type-v q8_0` |
| generation ceiling          | 11 tokens/call (12 → `IC0522`), same for Hindi and English           |
| Hindi quote                 | 11 `run_update` calls, terminates with EOG                           |

```bash
mkdir -p models/google/gemma-3-1b-it-GGUF
curl -L -o models/google/gemma-3-1b-it-GGUF/gemma-3-1b-it-Q4_K_M.gguf https://huggingface.co/ggml-org/gemma-3-1b-it-GGUF/resolve/main/gemma-3-1b-it-Q4_K_M.gguf

shasum -a 256 models/google/gemma-3-1b-it-GGUF/gemma-3-1b-it-Q4_K_M.gguf
# 8ccc5cd1f1b3602548715ae25a66ed73fd5dc68a210412eea643eb20eb75a135
```

`gemma3` is supported by the vendored fork (`fork/src/models/gemma3.cpp`) and
loads in wasm.

**Gemma has no system role.** Its template folds the system text into the user
turn — do not send `<|im_start|>system`:

```
<start_of_turn>user
{system}

{user}<end_of_turn>
<start_of_turn>model
```

Licensing: Gemma Terms of Use — not an OSI licence, but commercial use is
permitted. Llama-3.2-1B-Instruct is under the Llama 3.2 Community License.

## Models considered and ruled out

| model                    | size  | why not                                                      |
| ------------------------ | ----- | ------------------------------------------------------------ |
| Qwen3-1.7B               | 1.7 B | identical tokenizer to Qwen3-0.6B → no Hindi gain, lower ceiling |
| LilMoo-v0.2              | 0.7 B | **base model, no chat template** — best tokenizer measured, Apache 2.0, Llama architecture, purpose-built for Hindi. The most interesting candidate *if* you are ever willing to SFT it. |
| sarvam-1                 | 2 B   | base model **and** a non-commercial licence                   |
| Ganga-1B (LingoIITGN)    | 1 B   | base model, 2048 ctx, research preview                        |
| Param-1-2.9B-Instruct    | 2.9 B | genuinely Hindi-first and instruction-tuned, but 2.9 B puts the ceiling near 4 tokens/call (cf. LFM2.5-2.6B), and no GGUF is published |
| Tiny Aya (Cohere)        | 3.35 B| CC BY-NC-ND — non-commercial *and* no derivatives              |
| Bodhan AI (Sept 2026)    | —     | OCR / translation / TTS only, no general chat model            |

The pattern worth remembering: **every model with a genuinely good Hindi
tokenizer is either a base model or non-commercially licensed.** Gemma-3-1B-it is
the only instruction-tuned, deployable model that gets close.

## Does this remove the need for the v0.16.7 UTF-8 fix?

A fair question, since the model choice and the UTF-8 fix attack the same
symptom. They are independent, and the honest answer is "mostly, but not
entirely".

The v0.16.7 bug is that a `max_tokens` chunk boundary lands **inside** a
multi-byte codepoint, so the canister emits invalid UTF-8 in a Candid `text` and
the *calling* canister traps while decoding — uncatchably, in Motoko. That can
only happen when one codepoint spans more than one token.

Measured directly: encode a text, then for every prefix of the token list ask
whether the bytes so far are valid UTF-8. A prefix that is not is exactly a
boundary where v0.16.6 would have emitted a broken reply.

| text                                       | Gemma-3-1B  | Llama-3.2-1B | Qwen3-0.6B | LFM2.5-1.2B |
| ------------------------------------------ | ----------: | -----------: | ---------: | ----------: |
| generated Hindi aphorisms                  |    0 / 68   |     0 / 114  |   55 / 208 |   115 / 270 |
| the IConfucius Hindi prompt                |    0 / 56   |      0 / 86  |   46 / 169 |   104 / 229 |
| everyday nukta words (बड़ा, पढ़ना, सड़क)      |    0 / 13   |      0 / 31  |          — |           — |
| Urdu-origin letters (क़ ख़ ग़ ज़ फ़)            |    0 / 17   |      0 / 34  |          — |           — |
| emoji and symbols (😊 🙏 ₹ ✨)                 |    0 / 11   |  **4 / 18**  |          — |           — |
| rare Devanagari (vedic marks, ॐ, conjuncts)| **16 / 83** |   **20 / 84** |   45 / 120 |    66 / 141 |

**So: yes, for the Hindi IConfucius actually produces, both Gemma-3-1B and
Llama-3.2-1B would have worked on v0.16.6.** Zero unsafe boundaries means no
split, no invalid UTF-8, no dead caller. Qwen3-0.6B breaks on 26 % of boundaries
and LFM2.5 on 43 % — which is why the bug looked like "Hindi is broken" rather
than "this model is broken".

**But do not revert the fix**, for reasons the last two rows make concrete:

- **Emoji.** Llama-3.2 splits them, and models emit emoji unprompted — LFM2.5
  emitted 😊 and 🌟 in an unattended Hindi run on the canister during this
  investigation. That is a live path to a dead Motoko caller, not a theoretical one.
- **Rare Devanagari still byte-falls-back**, including in Gemma (24 byte-fallback
  tokens in that row). Vedic marks and unusual conjuncts do appear in
  philosophical and religious Hindi — exactly IConfucius's register.
- **The canister is general-purpose.** Any caller, any script, any model someone
  loads next — and the next model may well be byte-level again.
- **The fix is more than the carry.** It also sanitizes `conversation`,
  `prompt_remaining` and every chat in `get_chats`. Chat files written by v0.16.6
  and earlier already contain invalid bytes, so reverting re-opens the trap on
  reads of existing data.

A defensible middle path, if the carry's complexity is the concern: the
**stateless `utf8_sanitize` is what actually prevents the trap**; the per-session
**carry only improves fidelity**, turning what would be two `U+FFFD` into the
correct character. With a byte-level model that mattered enormously (up to 43 %
of boundaries); with Gemma-3-1B the carry would essentially never fire. Dropping
the carry while keeping the sanitize would remove the stateful, upgrade-fragile
part and keep the safety property. That is a real option — but it is a
simplification to make deliberately, not a reason to go back to v0.16.6.

One observed artifact worth knowing: with LFM2.5, at the ingestion→generation
boundary the first generation call can return 2 spurious `U+FFFD` before
otherwise intact text. No bytes are lost — it is the sanitizer converting what
would previously have trapped a strict Candid caller into replacement characters.
Gemma-3-1B showed no `U+FFFD` at all across every canister run here.

## Reproducing this

Tokenizer efficiency needs no model weights — only each repo's `tokenizer.json`
and the `tokenizers` package:

```bash
pip install tokenizers
curl -sL -o gemma3.json https://huggingface.co/unsloth/gemma-3-1b-it/resolve/main/tokenizer.json
python -c "from tokenizers import Tokenizer; print(len(Tokenizer.from_file('gemma3.json').encode('धैर्य वह मौन शक्ति है').ids))"
```

Generation quality was produced with a natively built `llama-server` from the
vendored fork at the exact commit compiled into the wasm, so native output
matches canister output. Ten topics, `--temp 0.7`, seeds 1000-1009, ctx 4096,
`--batch-size 8 --ubatch-size 8`, dual q8_0 KV.
