#pragma once

#include "wasm_symbol.h"

#include <cstdint>
#include <string>

struct llama_model;

// Typed decision models (TypeSafe System One, llama.cpp /v1/systemone).
// The model answers each question in one forward pass; no token is generated.
// See README-decision-models.md.
void run_decision() WASM_SYMBOL_EXPORTED("canister_update run_decision");
// The whole request in one update call: it yields to itself between steps.
void run_decision_full()
    WASM_SYMBOL_EXPORTED("canister_update run_decision_full");

// kev stored states, below .canister_cache/<caller>/sessions/<scope>/:
// remove a scope with all its files, and pin a state so that neither the
// caps nor the cleanup timer remove it before until_ns (IC time).
void remove_decision_scope()
    WASM_SYMBOL_EXPORTED("canister_update remove_decision_scope");
void pin_decision_state()
    WASM_SYMBOL_EXPORTED("canister_update pin_decision_state");
void unpin_decision_state()
    WASM_SYMBOL_EXPORTED("canister_update unpin_decision_state");

// Whether `file` is a stored kev state, or its stamp or pin file, that is
// pinned at now_ns (used by the cleanup timer)
bool decision_state_pinned(const std::string &file, uint64_t now_ns);

// Removes a stored kev state with its stamp and pin files
void remove_decision_state(const std::string &state_path);

// Lifecycle: load_model calls decision_init after a successful load, and
// icpp_free_model calls decision_reset.
void decision_init(const llama_model *model);
void decision_reset();

// True when the loaded model is a decision model (of any decision type).
bool decision_model_loaded();
