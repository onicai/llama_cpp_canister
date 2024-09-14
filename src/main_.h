#pragma once

#include <sstream>

int main_(int argc, char **argv, std::string principal_id, bool load_model_only, std::string &icpp_error_msg, std::ostringstream &input_ss, std::ostringstream &output_ss);
void free_model();
void reset_static_memory();