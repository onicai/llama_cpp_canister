#pragma once

#include <sstream>

// Forward declaration for llama_model
struct llama_model;
struct llama_context;

// Global model pointer (defined in main_.cpp)
extern llama_model **g_model;

// The context persisted in Orthogonal Persistence, or nullptr when no model
// is loaded. Used by the prompt-cache stamp to record the context layout.
llama_context *icpp_persisted_ctx();

// llama-cli's main, run for one chunk: it loads the session, decodes the
// prompt and generates, then saves the session for the next call. It stops after
// max_tokens tokens, or before max_instructions instructions when > 0.
// n_predict_reached, when not null, is set when generation stopped on -n.
int main_(int argc, char **argv, std::string principal_id, bool load_model_only,
          std::string &icpp_error_msg, std::ostringstream &conversation_ss,
          std::ostringstream &output_ss, const uint64_t &max_tokens,
          std::string &prompt_remaining, bool &generated_eog,
          uint64_t &n_prompt_tokens, uint64_t &n_prompt_tokens_cached,
          uint64_t &n_prompt_tokens_decoded, uint64_t &n_tokens_generated,
          uint64_t &n_prompt_tokens_remaining, uint64_t max_instructions = 0,
          bool *n_predict_reached = nullptr);

void icpp_free_model();
void reset_static_memory();