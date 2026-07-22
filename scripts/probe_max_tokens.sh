#!/usr/bin/env bash
# Probe the generation token-ceiling for the deployed Qwen2.5 model on mainnet.
# For a candidate N: fresh new_chat -> prefill at a safe rate -> set N ->
# ONE post-prefill generation call. Reports OK (with #gen tokens / eog) vs the
# IC0522 instruction-limit trap. Fresh prefill each time keeps the starting
# context identical (~same prompt) so probes are comparable.
#
# Usage: probe_max_tokens.sh <N> [prefill_rate]
set -uo pipefail
N="${1:?need candidate N}"
PREFILL_RATE="${2:-10}"
CID=llama_cpp
export DFX_WARNING=-mainnet_plaintext_identity
G(){ grep -viE "deprecated|icp-cli|LLM skills|DEPRECATION|metadata|candid:service"; }

PROMPT='<|im_start|>system\nYou are a helpful assistant.<|im_end|>\n<|im_start|>user\ngive me a short introduction to LLMs.<|im_end|>\n<|im_start|>assistant\n'
ING="(record { args = vec { \"--prompt-cache\"; \"prompt.cache\"; \"--prompt-cache-all\"; \"--cache-type-k\"; \"q8_0\"; \"--repeat-penalty\"; \"1.1\"; \"--temp\"; \"0.6\"; \"-sp\"; \"-p\"; \"${PROMPT}\"; \"-n\"; \"1\" } })"
GEN='(record { args = vec { "--prompt-cache"; "prompt.cache"; "--prompt-cache-all"; "--cache-type-k"; "q8_0"; "--repeat-penalty"; "1.1"; "--temp"; "0.6"; "-sp"; "-p"; ""; "-n"; "512" } })'

# 1) reset chat
dfx canister --network ic call $CID new_chat '(record { args = vec { "--prompt-cache"; "prompt.cache"; "--cache-type-k"; "q8_0"; } })' >/dev/null 2>&1

# 2) prefill at safe rate
dfx canister --network ic call $CID set_max_tokens "(record { max_tokens_query = 1 : nat64; max_tokens_update = ${PREFILL_RATE} : nat64 })" >/dev/null 2>&1
for i in $(seq 1 15); do
  OUT=$(dfx canister --network ic call $CID run_update "$ING" 2>&1 | G)
  echo "$OUT" | grep -qE 'prompt_remaining = ""' && break
done

# 3) set candidate N
dfx canister --network ic call $CID set_max_tokens "(record { max_tokens_query = 1 : nat64; max_tokens_update = ${N} : nat64 })" >/dev/null 2>&1

# 4) one measured generation call
OUT=$(dfx canister --network ic call $CID run_update "$GEN" 2>&1 | G)
if echo "$OUT" | grep -qiE "IC0522|exceeded the limit of .* instructions"; then
  echo "N=${N}: TRAP (IC0522 instruction limit exceeded)"
elif echo "$OUT" | grep -qE 'status_code = 200'; then
  EOG=$(echo "$OUT" | grep -oE 'generated_eog = (true|false)')
  FRAG=$(echo "$OUT" | grep -oE 'output = "[^"]*"')
  echo "N=${N}: OK  ($EOG)  $FRAG"
else
  echo "N=${N}: OTHER ->"; echo "$OUT" | grep -iE "Err|reject|error" | head -3
fi
