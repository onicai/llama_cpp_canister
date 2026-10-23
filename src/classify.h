#pragma once

#include "wasm_symbol.h"

// Sequence classifiers (BERT / ModernBERT with a classification head), loaded
// with --embedding --pooling rank, as llama-embedding and llama-server take
// them. One text per call: one forward pass, which returns the raw scores of
// the classifier head with their labels. See docs/README-classifier-models.md.
void run_classify() WASM_SYMBOL_EXPORTED("canister_update run_classify");
void run_classify_query()
    WASM_SYMBOL_EXPORTED("canister_query run_classify_query");

// True when the loaded model is a sequence classifier: rank pooling, and a
// classification head with labels.
bool classify_model_loaded();
