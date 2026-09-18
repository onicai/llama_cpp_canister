# Contributors Guide

# Setup

Follow steps of [llama_cpp_canister/README/Getting Started](https://github.com/onicai/llama_cpp_canister/blob/main/README.md#getting-started)

# How to upgrade llama.cpp

## Sync fork
In GitHub, `Sync fork` for master branch of https://github.com/onicai/llama_cpp_onicai_fork

## Fetch the tags from upstream repo

`llama.cpp` continously creates new releases, named `bxxxx`

You can fetch the tags from these releases and add them to our forked repo:

After cloning the `llama_cpp_onicai_fork` repo to you local computer:

```
# From llama_cpp_onicai_fork
git remote add upstream https://github.com/ggml-org/llama.cpp.git

# after this, the tags will apear locally
git fetch upstream --tags

# after this, the tags will appear in GitHub
git push origin --tags
```

## llama_cpp_onicai_fork: setup a local branch
Take following steps locally:
- git fetch 

- These are the git-sha values of the llama.cpp versions we branched from:

  | upgrade # | llama.cpp sha | llama.cpp release-tag |    date    |
  | --------- | ------------- | --------------------- | ---------- |
  |    0002   |     615212    |         b4532         | Feb  2 '25 |
  |    0001   |     b841d0    |         -             | Oct 18 '24 |
  |    0000   |     5cdb37    |         -             | Jul 21 '24 |


- Start with a fresh clone of llama_cpp_onicai_fork:
  ```bash
  # From folder: llama_cpp_canister\src

  # Copy old version, as a reference to use with meld
  # This is just as a reference. You can remove this folder once all done.
  # (-) Make sure the current `onicai` branch is checked out.
  #     The one that branched off from `git-sha-old`
  cp llama_cpp_onicai_fork llama_cpp_onicai_fork_<git-sha-old>

  # Clone the new version in place
  git clone git@github.com:onicai/llama_cpp_onicai_fork.git
  ```

- In llama_cpp_onicai_fork, from master, create a new branch: `onicai-<git-sha-new>`

  For `git-sha-new`, use the short commit sha from which we're branching.

## Update all files

Unless something was drastically changed in llama.cpp, it is sufficient to just re-upgrade the files 
listed in [icpp.toml](https://github.com/onicai/llama_cpp_canister/blob/main/icpp.toml), plus their
header files.

As you do your upgrade, modify the descriptions below, to help with the next upgrade:
We use `meld` for comparing the files:

```bash
brew install --cask dehesselle-meld
```

## Details for each upgrade

See the files: README-<upgrade #>-<llama.cpp sha>.md

## Recommended porting/validation order

Established during the b10076 upgrade (see `README-0003-305ba519.md`). Native tests pass
almost everything the real canister will reject, so escalate through four gates in order —
do NOT skip to an icp deploy:

1. **Native** (`make all-tests` / MockIC) — fastest loop; catches API/merge/link errors and
   verifies exact-token output. But native has real mmap, threads, exceptions, stack and
   getenv, so a green native suite proves almost nothing about canister *runtime* behavior.
   (In b10076, native was 111/111 while five wasm-only bugs were still live.)

2. **Faithful wasmtime harness** (`scripts/wasm_harness.py`) — BEFORE deploying. The IC
   gives no wasm backtrace for a trap; this does.
   - Run the **pre-optimize** wasm `build/llama_cpp_before_opt.wasm` so backtraces show
     function NAMES (binaryen's `optimize()` strips the name section from the deployed wasm).
   - Just instantiating runs the C++ ctors → catches static-init faults. Pass `--method` (and
     a `didc encode`d arg) to reach faults deeper inside `load_model`/`run_update`.
   - Then run the optimized `build/llama_cpp.wasm` too, to confirm `optimize()` did not change
     behavior.

3. **Local IC replica** — confirmation, not primary debugging.
   - ALWAYS `icp deploy` or `icp canister install --wasm build/llama_cpp.wasm`; the `.icp/cache`      cache can serve a stale binary. Verify the module hash changed after install.
   - Run the full pipeline: upload → `load_model` → `new_chat` → `run_update`.

4. **Mainnet** — throughput / behavior under the real 40B instruction cap.

**Interpretation rule that saves the most time:** if `scripts/wasm_harness.py` says the binary
is clean but the IC traps, suspect the DEPLOY PIPELINE (stale `.icp/cache` cache, wrong `--wasm`),
not the binary. In b10076, a multi-hour "install trap" chase was ultimately the deploy pipeline installing a
stale cached wasm.

### Gate 0: plain llama.cpp on the host, for fast iteration

Before any of the four gates above, remember the vendored fork is just llama.cpp — you can build
and run its normal `llama-cli` / `llama-server` on your machine, against the same gguf files in
`models/`. No canister, no replica, no 20-minute wasm build, and you get llama.cpp's full logging
at native speed.

Use it whenever the question is "what does llama.cpp actually do here?" rather than "how does the
IC react to it?" — model/context/KV behaviour, allocation sizes and counts, tokenization, sampler
behaviour, what a flag changes. It is far faster than instrumenting the canister for the same
answer.

```bash
cd src/llama_cpp_onicai_fork
cmake -B build && cmake --build build -j        # or: make
./build/bin/llama-cli -m ../../models/Qwen/Qwen3-1.7B-GGUF/Qwen3-1.7B-Q4_K_M.gguf \
    -c 16384 --batch-size 8 --ubatch-size 8 --cache-type-k q8_0 --cache-type-v q8_0 -v -p "hi"
```

`-v` (`--verbose`, sets verbosity to INT_MAX) is the flag that turns on llama.cpp's INFO logging —
`llama_context: n_ctx`, `llama_kv_cache: CPU KV buffer size = ... MiB`, `graph_reserve`,
`sched_reserve`, and so on. Those lines are how the KV allocation was sized and counted during the
IC0522 investigation.

The same `-v` works through `load_model` on a canister, which is how to get those lines out of a
replica — but note the canister's log ring defaults to 4 KiB and needs raising first:

```bash
icp canister settings update <canister> --log-memory-limit 2mib -n ic   # max 2 MiB
icp canister logs <canister> -n ic                                       # controller-only
```

Canister log records are FRAGMENTS of lines and must be reassembled in `index` order to be
readable. Logs survive a rejected message (you see how far execution got, which is often the whole
diagnosis), but the ring is small, so capture right after the call.

## Three classes of bug that upstream does NOT have — re-check all three on every upgrade

These are not llama.cpp bugs, so there is nothing to send upstream. They exist only because
of things this canister does that `llama-cli` never does. Each produced canister traps that
were found and fixed here (classes 1-2 in v0.16.2, class 3 in v0.17.0); new upstream code can
reintroduce any of them at any time, so walk the checklists below on each upgrade.

### 1. Upstream's `try`/`catch` error handling is DEAD here — a throw is a trap

`src/wasi-exception-stubs.cpp` makes `__cxa_throw` call `IC_API::trap()` and
`__cxa_begin_catch`/`__cxa_end_catch` no-ops. So **every upstream API whose contract is
"catch internally, return an error code" instead traps the canister**, and the trap also
rolls back the whole message.

Real example (fixed in v0.16.2): `llama_state_load_file` wraps its call in `try`/`catch` and
returns `false` on a bad session file (`llama-context.cpp`, look for
`catch (const std::exception & err)`). Upstream `main.cpp` therefore reports a clean error
when a prompt cache was written by a different model. In the canister the same path threw
`wrong model arch: 'llama' instead of 'qwen3'` and trapped (`IC0503`) — and, because the bad
cache stayed on disk, every later call for that principal re-trapped.

**On each upgrade:** re-run this and check every hit that we call, because upstream adds
new ones:

```bash
grep -n -B8 "catch (const std::exception" src/llama_cpp_onicai_fork/src/llama-context.cpp
```

At b10076 the catch-and-return-error entry points are `llama_state_load_file`,
`llama_state_save_file`, `llama_state_seq_save_file`, `llama_state_seq_load_file`, and
`llama_context::state_{get_size,get_data,set_data}`. We call `llama_state_save_file` on
**every** `run_update`. For any such API we call, validate the precondition BEFORE calling in
(as `prompt_cache_discard_if_stale()` now does), rather than relying on the return value.

A useful corollary for diagnosis: a C++ throw surfaces as **`IC0503`** with an
`UNCAUGHT C++ EXCEPTION [type]: message` payload, whereas a failed `GGML_ASSERT` or a real
memory fault surfaces as **`IC0502`**. The error code alone tells you which class you are in.

### 2. The `llama_context` outlives the `common_params` that created it

`load_model` builds the context once and it is Orthogonally Persisted; every later
`run_update`/`run_query` re-parses a **different** argv that does not repeat the load-time
flags. So in `src/main_.cpp` any `params.<x>` that upstream implicitly assumes equals the
context's `cparams.<x>` is **stale — it is the `common_params` default**, not what the
context has.

Upstream cannot hit this: `common_init_from_params` copies `params` into `cparams`
(`common/common.cpp`, e.g. `cparams.n_batch = params.n_batch`) in the same process, so the two
can never diverge there.

Real example (fixed in v0.16.2): the decode loop clamped `n_eval` to `params.n_batch` — the
2048 default — while `llama_decode` asserts `n_tokens_all <= cparams.n_batch`, which was 8
(`--batch-size 8` at load). The clamp was dead code, and any `max_tokens_update` above the
loaded `--batch-size` failed `GGML_ASSERT` and trapped (`IC0502 unreachable`).

**On each upgrade:** after re-porting `main_.cpp`, audit every `params.` read in it and ask
"is this a load-time property of the context?". If it is, take it from the context instead:

```bash
grep -n "params\." src/main_.cpp
```

The context-owned properties, and the `common_params` field each is copied from at load time
(`common/common.cpp`, `common_init_from_params`) — read the LEFT column in `main_.cpp`, never
the right:

| take this from the context | NOT this per-call field |
| -------------------------- | ----------------------- |
| `llama_n_ctx(ctx)`         | `params.n_ctx`          |
| `llama_n_batch(ctx)`       | `params.n_batch`        |
| `llama_n_ubatch(ctx)`      | `params.n_ubatch`       |
| `llama_n_seq_max(ctx)`     | `params.n_parallel`     |

(`main_.cpp` already reads `n_ctx` correctly — it was only the batch sizes that were wrong.)
Sampling settings, the prompt-cache path and the other per-call flags are genuinely per-call
and must keep coming from `params`.

### 3. A per-call resource handed to the persisted context dangles after the call

The `llama_context` (and the ggml backend inside it) is Orthogonally Persisted, but the
things `main_.cpp` attaches to it each call — the **threadpool**, the **sampler**, the **log
file**, and any **callback** — are created fresh and freed at end of call. If the context (or
backend) keeps a pointer to one of them past the free, the NEXT call dereferences freed heap.
Upstream cannot hit this: in `llama-cli` the process ends when the call ends, so "freed at end
of call" and "context destroyed" are the same moment.

Real example (fixed in v0.17.0, full write-up in `README-0003-305ba519-IC0502.md`): every
call did `llama_attach_threadpool(ctx, ...)` then freed the pool at the end **without
detaching**. The pointer survived in two persisted places — `ctx->threadpool` and the CPU
backend's own `ggml_backend_cpu_context::threadpool` (latched in `graph_compute` via
`ggml_backend_cpu_set_threadpool`). On the next call that setter called
`ggml_threadpool_pause()` on the **freed** pool; the pool's pthread mutex sits at offset 0,
right over dlmalloc's free-list links, so the pause corrupted the allocator and a **later,
unrelated** `malloc`/`free` trapped `IC0502 heap out of bounds`. It was intermittent (only
when the new pool landed at a different address) and sticky (the corruption lives in the
persisted heap, so `stop`/`start` never cleared it — only a `--mode upgrade`/`reinstall`
heap reset did). It shipped in v0.16.6 and hit the funnAI prd fleet for days.

**On each upgrade:** find everything the per-call code registers into the persisted context or
backend, and confirm each is torn down before its backing memory is freed.

```bash
# per-call things attached/registered into the persisted ctx or backend:
grep -nE "llama_(attach|detach)_threadpool|set_threadpool|set_abort_callback|cb_eval|common_sampler_(init|free)|common_log_set_file" src/main_.cpp
# and, in the fork, who stores a raw pointer to such a per-call object:
grep -rnE "threadpool|abort_callback|cb_eval_user_data" src/llama_cpp_onicai_fork/src/llama-context.h
```

The `CallTeardown` guard in `main_.cpp` is what frees the threadpool + sampler + log on every
exit path (including early returns), and it detaches the threadpool first. If a re-port adds a
NEW per-call registration into the context (a new callback, a new attach), it must be added to
that guard's teardown, and any fork-side setter that pauses/touches the *old* pointer on
replacement must be made safe under `__wasi__` (see the `ggml_backend_cpu_set_threadpool`
ICPP-PATCH).

**MANDATORY regression gate — CI does NOT cover this.** The tiny/gemma QA iterations never
reproduced this bug: it needs the fleet's own model plus realistic allocation churn plus many
reuse cycles to make a freed pool land at a fresh address. `test/test_ic0502_repro.py` does
exactly that — it replays the real funnAI Judge/ShareService/Challenger controller sequence,
with the actual prd prompts, against Qwen2.5-0.5B (the fleet model) loaded as prd loads it.
It is wired into the QA driver as a `local_only` iteration, so **it runs on
`make test-llm-wasm` but is skipped on the hosted GitHub runner** (the full-ctx Qwen replay
times out there). Before shipping any llama.cpp upgrade, run it locally with a real soak:

```bash
# via the QA driver (uploads Qwen2.5-0.5B, deploys, runs the replay):
make test-llm-wasm
# or directly, with a longer soak, against an already-deployed canister:
IC0502_REPRO_CYCLES=50 pytest -vv --network local --identity llama-cpp-testing test/test_ic0502_repro.py
```

A regression traps `IC0502 heap out of bounds` (the unfixed build trapped in cycle 0); a
healthy build runs the full soak clean with `graphs reused` climbing (graph reuse stays on).

### 4. The v0.16.2 fixes live in `main_.cpp` — RE-APPLY them after the re-port

`src/main_.cpp` is a **port of upstream's** `tools/completion/completion.cpp`, re-done on
every upgrade (see "C5 — the `main_.cpp` re-port" in `README-0003-305ba519.md`). It carries
~93 `ICPP-PATCH` markers, and patches HAVE been lost in a re-port before — b10076's Bug #3
(`use_mmap`) and Bug #4 (warmup) were both silently dropped in the merge and only resurfaced
as runtime traps on the replica.

So the fixes for the two classes above are **not** self-sustaining:

| File | Survives a re-port? |
| ---- | ------------------- |
| `src/promptcache.{h,cpp}` | **Yes** — canister-owned, no upstream equivalent |
| `src/main_.cpp` | **No** — re-ported from upstream; every `ICPP-PATCH` must be re-applied |

The five edits in `main_.cpp` to re-apply, all marked `ICPP-PATCH`:

| # | What | Why it must come back |
| - | ---- | --------------------- |
| 1 | Define `n_batch_ctx` / `n_ubatch_ctx` from `llama_n_batch(ctx)` / `llama_n_ubatch(ctx)`, next to `const int n_ctx = llama_n_ctx(ctx);` | the single source for 2-4 below |
| 2 | Decode loop: stride `i += n_batch_ctx` and clamp `n_eval > n_batch_ctx` (upstream uses `params.n_batch` in both) | oversized batch ⇒ `GGML_ASSERT` ⇒ `IC0502` trap |
| 3 | Prompt-fill loop: break at `embd.size() >= n_batch_ctx` (upstream: `params.n_batch`) | keeps `embd` to one batch so the decode loop is single-chunk |
| 4 | `max_tokens` break: append `[embd.begin() + i, embd.begin() + i + n_eval)` (not from `begin()`) | wrong offset silently corrupts the prompt cache if `embd` ever spans chunks |
| 5 | Call `prompt_cache_discard_if_stale()` before `llama_state_load_file`, and `prompt_cache_write_format_stamp()` after `llama_state_save_file` | without the first, a foreign cache traps; without the second, a discarded cache cold-starts forever |

Verify after the re-port — all of these must return hits:

```bash
grep -n "n_batch_ctx" src/main_.cpp                      # items 1-3: expect ~10 hits
grep -n "embd.begin() + i" src/main_.cpp                 # item 4:    expect 3 hits
grep -n "prompt_cache_discard_if_stale\|prompt_cache_write_format_stamp" \
        src/main_.cpp                                    # item 5:    expect 2 hits
# and the inverse -- params.n_batch must survive only in COMMENTS, never in code:
grep -n "params\.n_batch" src/main_.cpp | grep -v "//"   # expect NO output
```

⚠️ **Neither fix has an automated regression test yet**, so these greps are currently the only
guard — a lost patch will not fail `make all-tests`, it will trap on the replica. Two cheap
end-to-end checks that do catch them:

```bash
# item 2/3: max_tokens ABOVE the loaded --batch-size must give a clean IC0522, never a trap
#   load with --batch-size 8, set_max_tokens update=40, then run_update with a prompt
# item 5: a prompt cache from another model must self-heal, not trap
#   load model A -> new_chat + run_update -> load model B -> run_update on the SAME cache
#   expect Ok with n_prompt_tokens_cached = 0 (cold start), not a trap
```

## Branch management

We need to rethink this logic, but for now it is ok...

### llama_cpp_onicai_fork
Do NOT merge the `onicai-<git-sha>` branch into the `onicai` branch, but replace it:

```
# do the onicai branch management while master branch is checked out
git checkout master
git branch -m onicai onicai-<git-sha-old>
git branch -m onicai-<git-sha-new> onicai
git push --force origin onicai:onicai
git push origin onicai-<git-sha-old>:onicai-<git-sha-old>
#
# Switch to the onicai branch, which now contains the <git-sha-new> version
git checkout onicai
```

## llama_cpp_canister

Merge the `onicai-<git-sha>` branch into the `onicai` branch