#pragma once

#include <fstream>

bool open_ifstream(const std::string &f_name,
                   const std::ios_base::openmode &mode,
                   std::ifstream &if_stream, std::string &msg);
bool open_ofstream(const std::string &f_name,
                   const std::ios_base::openmode &mode,
                   std::ofstream &if_stream, std::string &msg);