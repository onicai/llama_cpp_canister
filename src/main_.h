#pragma once

#include <sstream>

// Forward declaration for llama_model
struct llama_model;

// Global model pointer (defined in main_.cpp)
extern llama_model **g_model;

int main_(int argc, char **argv, std::string principal_id, bool load_model_only,
          std::string &icpp_error_msg, std::ostringstream &conversation_ss,
          std::ostringstream &output_ss, const uint64_t &max_tokens,
          std::string &prompt_remaining, bool &generated_eog);

void icpp_free_model();
void reset_static_memory();