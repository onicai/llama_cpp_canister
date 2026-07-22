// WASI stubs for llama.cpp's chat-template and JSON-schema-grammar machinery.
//
// common/chat*.cpp, common/jinja/* and common/json-schema-to-grammar.cpp are
// excluded from icpp.toml to keep the WASM globals count under ICP's limit
// (see icpp.toml for the full exclusion list). The canister does not run
// conversation mode and exposes no grammar / JSON-schema surface in
// src/llama_cpp.did — but common/arg.cpp still references these symbols while
// parsing and documenting CLI options, so they must link.
//
// Anything that would actually constrain generation traps: silently returning
// an empty grammar would let a --grammar request produce unconstrained output,
// which is worse than a clear failure.
//
// Adapted from instruction-bounded-inference-artifact
// MIT (c) 2026 Julien Aerni, Simeon Fluck, Dustin Becker

#include "common/chat.h"
#include "common/json-schema-to-grammar.h"

#include <nlohmann/json.hpp>

#include "ic_api.h"

// json-schema-to-grammar.h only forward-declares this; common_schema_info holds
// it in a unique_ptr, so the (defaulted) destructor needs a complete type.
class common_schema_converter {};

bool common_chat_verify_template(const std::string &tmpl, bool use_jinja) {
  // Chat templates are not compiled in; report unsupported rather than
  // claiming a template is valid.
  return false;
}

common_reasoning_format
common_reasoning_format_from_name(const std::string &format) {
  if (format == "none") {
    return COMMON_REASONING_FORMAT_NONE;
  }
  IC_API::trap("common_reasoning_format_from_name: reasoning formats are not "
               "supported in llama_cpp_canister (got '" +
               format + "')");
}

std::string json_schema_to_grammar(const nlohmann::ordered_json &schema,
                                   bool force_gbnf) {
  IC_API::trap("json_schema_to_grammar: JSON-schema grammars are not supported "
               "in llama_cpp_canister");
}

std::string gbnf_format_literal(const std::string &literal) {
  IC_API::trap(
      "gbnf_format_literal: GBNF grammars are not supported in llama_cpp_canister");
}

std::string
build_grammar(const std::function<void(const common_grammar_builder &)> &cb,
              const common_grammar_options &options) {
  IC_API::trap(
      "build_grammar: GBNF grammars are not supported in llama_cpp_canister");
}

common_schema_info::common_schema_info() = default;
common_schema_info::~common_schema_info() = default;
common_schema_info::common_schema_info(common_schema_info &&) noexcept =
    default;
common_schema_info &
common_schema_info::operator=(common_schema_info &&) noexcept = default;

void common_schema_info::resolve_refs(nlohmann::ordered_json &schema) {}

bool common_schema_info::resolves_to_string(
    const nlohmann::ordered_json &schema) {
  return false;
}
