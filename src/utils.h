#pragma once

#include <fstream>

#include "ic_api.h"

bool open_ifstream(const std::string &f_name,
                   const std::ios_base::openmode &mode,
                   std::ifstream &if_stream, std::string &msg);
bool open_ofstream(const std::string &f_name,
                   const std::ios_base::openmode &mode,
                   std::ofstream &if_stream, std::string &msg);

std::tuple<int, std::vector<char *>, std::unique_ptr<std::vector<std::string>>>
get_args_for_main(IC_API &ic_api);