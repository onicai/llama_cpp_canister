#pragma once

#include <fstream>
#include <system_error>

#include "ic_api.h"

bool open_ifstream(const std::string &f_name,
                   const std::ios_base::openmode &mode,
                   std::ifstream &if_stream, std::string &msg);
bool open_ofstream(const std::string &f_name,
                   const std::ios_base::openmode &mode,
                   std::ofstream &if_stream, std::string &msg);

std::tuple<int, std::vector<char *>, std::unique_ptr<std::vector<std::string>>>
get_args_for_main(IC_API &ic_api);

bool my_create_directory(const std::filesystem::path &dir_path,
                         std::string &error_msg);

void send_output_record_result_error_to_wire(IC_API &ic_api,
                                             uint16_t http_status_code,
                                             const std::string &error_msg);

std::filesystem::file_time_type
get_last_write_time(const std::filesystem::path &file, std::error_code &ec);