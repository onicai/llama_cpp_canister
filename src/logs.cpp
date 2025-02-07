#include "logs.h"
#include "auth.h"
#include "common.h"
#include "db_chats.h"
#include "http.h"
#include "main_.h"
#include "max_tokens.h"
#include "utils.h"

#include "arg.h"
#include "log.h"

#include <filesystem>
#include <iostream>
#include <string>
#include <system_error>

#include "ic_api.h"

static void print_usage(int argc, char **argv) {
  // do nothing function
}

void remove_log_file() {
  IC_API ic_api(CanisterUpdate{std::string(__func__)}, false);
  std::string error_msg;
  if (!is_caller_a_controller(ic_api)) {
    error_msg = "Access Denied.";
    send_output_record_result_error_to_wire(
        ic_api, Http::StatusCode::Unauthorized, error_msg);
    return;
  }

  auto [argc, argv, args] = get_args_for_main(ic_api);

  // Process the args, which will instantiate the log singleton
  common_params params;
  if (!common_params_parse(argc, argv.data(), params, LLAMA_EXAMPLE_MAIN,
                           print_usage)) {
    error_msg = "Cannot parse args.";
    send_output_record_result_error_to_wire(
        ic_api, Http::StatusCode::InternalServerError, error_msg);
    return;
  }

  // Now we can remove the log file
  std::string msg;
  bool success = common_log_remove_file(common_log_main(), msg);
  if (!success) {
    send_output_record_result_error_to_wire(
        ic_api, Http::StatusCode::InternalServerError, msg);
    return;
  }

  // Return output over the wire
  CandidTypeRecord r_out;
  r_out.append("status_code", CandidTypeNat16{Http::StatusCode::OK}); // 200
  r_out.append("conversation", CandidTypeText{""});
  r_out.append("output", CandidTypeText{msg});
  r_out.append("error", CandidTypeText{""});
  r_out.append("prompt_remaining", CandidTypeText{""});
  r_out.append("generated_eog", CandidTypeBool{false});
  ic_api.to_wire(CandidTypeVariant{"Ok", r_out});
}

void log_pause() {
  IC_API ic_api(CanisterUpdate{std::string(__func__)}, false);
  if (!is_caller_a_controller(ic_api)) return;

  common_log_pause(common_log_main());

  CandidTypeRecord status_code_record;
  status_code_record.append("status_code", CandidTypeNat16{200});
  ic_api.to_wire(CandidTypeVariant{"Ok", status_code_record});
}

void log_resume() {
  IC_API ic_api(CanisterUpdate{std::string(__func__)}, false);
  if (!is_caller_a_controller(ic_api)) return;

  common_log_resume(common_log_main());

  CandidTypeRecord status_code_record;
  status_code_record.append("status_code", CandidTypeNat16{200});
  ic_api.to_wire(CandidTypeVariant{"Ok", status_code_record});
}