#include "max_tokens.h"

#include <iostream>
#include <string>

#include "auth.h"

#include "ic_api.h"

uint64_t max_tokens_update{0}; // 0 = no limit
uint64_t max_tokens_query{0};  // 0 = no limit

void set_max_tokens() {
  IC_API ic_api(CanisterUpdate{std::string(__func__)}, false);
  if (!is_caller_a_controller(ic_api)) return;

  CandidTypeRecord r_in;
  r_in.append("max_tokens_update", CandidTypeNat64{&max_tokens_update});
  r_in.append("max_tokens_query", CandidTypeNat64{&max_tokens_query});
  ic_api.from_wire(r_in);

  CandidTypeRecord status_code_record;
  status_code_record.append("status_code", CandidTypeNat16{200});
  ic_api.to_wire(CandidTypeVariant{"Ok", status_code_record});
}

void get_max_tokens() {
  IC_API ic_api(CanisterQuery{std::string(__func__)}, false);

  CandidTypeRecord r_out;
  r_out.append("max_tokens_update", CandidTypeNat64{max_tokens_update});
  r_out.append("max_tokens_query", CandidTypeNat64{max_tokens_query});

  ic_api.to_wire(r_out);
}