// WASI stubs for llama.cpp's speculative-decoding helpers.
//
// common/speculative.cpp is excluded from icpp.toml to keep the WASM globals
// count under ICP's limit. A canister does not run a draft model, but
// common/arg.cpp references these helpers when building --help text for the
// speculative CLI options, so the symbols must exist.
//
// Only the strings used by arg parsing are implemented; anything that would
// actually run speculative decoding is absent by construction (the
// common_speculative struct is never instantiated).
//
// Adapted from instruction-bounded-inference-artifact
// MIT (c) 2026 Julien Aerni, Simeon Fluck, Dustin Becker

#include "common/speculative.h"

std::string common_speculative_type_name_str(
    const std::vector<enum common_speculative_type> &types) {
  return "";
}

const char *common_speculative_all_types_str() { return ""; }

std::vector<enum common_speculative_type>
common_speculative_types_from_names(const std::vector<std::string> &names) {
  return {};
}
