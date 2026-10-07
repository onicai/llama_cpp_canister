#pragma once

#include "wasm_symbol.h"

struct llama_model;

// Typed decision models (TypeSafe System One, llama.cpp /v1/systemone).
// The model answers each question in one forward pass; no token is generated.
// See README-decision-models.md.
void run_decision() WASM_SYMBOL_EXPORTED("canister_update run_decision");

// Lifecycle: load_model calls decision_init after a successful load, and
// icpp_free_model calls decision_reset.
void decision_init(const llama_model *model);
void decision_reset();

// True when the loaded model is a decision model (of any decision type).
bool decision_model_loaded();
