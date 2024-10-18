#pragma once

#include <sstream>

int main_(int argc, char **argv, std::string principal_id, bool load_model_only,
          std::string &icpp_error_msg, std::ostringstream &conversation_ss,
          std::ostringstream &output_ss, const uint64_t &max_tokens,
          std::string &prompt_remaining, bool &generated_eog);
void free_ctx();
void free_model();
void reset_static_memory();