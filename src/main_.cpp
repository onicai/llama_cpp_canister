// ICPP-PATCH-START
// Internet Computer SmartContract version of: examples/main/main.cpp
// See: https://github.com/onicai/llama_cpp_onicai_fork/tree/master/examples/main/README.md
#include "main_.h"
#include "ic_api.h"
#include "promptcache.h"
#include "utils.h"
// ICPP-PATCH-END
#include "arg.h"
#include "chat-template.hpp"
#include "common.h"
#include "console.h"
#include "llama.h"
#include "log.h"
#include "sampling.h"

#include <cstdio>
#include <cstring>
#include <ctime>
#include <format>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#if defined(__unix__) || (defined(__APPLE__) && defined(__MACH__))
#include <signal.h>
#include <unistd.h>
#elif defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <signal.h>
#include <windows.h>
#endif

#if defined(_MSC_VER)
#pragma warning(disable : 4244 4267) // possible loss of data
#endif

static const char *DEFAULT_SYSTEM_MESSAGE = "You are a helpful assistant";

static llama_context **g_ctx = nullptr;
// static llama_model             ** g_model; // Make this a global variable, accessible from common.cpp
llama_model **g_model = nullptr;
static common_sampler **g_smpl = nullptr;
static common_params *g_params = nullptr;
static std::vector<llama_token> *g_input_tokens = nullptr;
static std::ostringstream *g_output_ss = nullptr;
static std::vector<llama_token> *g_output_tokens = nullptr;
static bool is_interacting = false;
static bool need_insert_eot = false;

static void print_usage(int argc, char **argv) {
  (void)argc;

  LOG("\nexample usage:\n");
  LOG("\n  text generation:     %s -m your_model.gguf -p \"I believe the meaning of life is\" -n 128\n",
      argv[0]);
  LOG("\n  chat (conversation): %s -m your_model.gguf -p \"You are a helpful assistant\" -cnv\n",
      argv[0]);
  LOG("\n");
}

static bool file_exists(const std::string &path) {
  std::ifstream f(path.c_str());
  return f.good();
}

static bool file_is_empty(const std::string &path) {
  std::ifstream f;
  f.exceptions(std::ifstream::failbit | std::ifstream::badbit);
  f.open(path.c_str(), std::ios::in | std::ios::binary | std::ios::ate);
  return f.tellg() == 0;
}

//icpp-start NO CONSOLE
// #if defined (__unix__) || (defined (__APPLE__) && defined (__MACH__)) || defined (_WIN32)
// static void sigint_handler(int signo) {
//     if (signo == SIGINT) {
//         if (!is_interacting && g_params->interactive) {
//             is_interacting  = true;
//             need_insert_eot = true;
//         } else {
//             console::cleanup();
//             LOG("\n");
//             common_perf_print(*g_ctx, *g_smpl);
//
//             // make sure all logs are flushed
//             LOG("Interrupted by user\n");
//             common_log_pause(common_log_main());
//
//             _exit(130);
//         }
//     }
// }
// #endif
//icpp-end NO CONSOLE

int main_(int argc, char **argv, std::string principal_id, bool load_model_only,
          std::string &icpp_error_msg, std::ostringstream &conversation_ss,
          std::ostringstream &output_ss, const uint64_t &max_tokens,
          std::string &prompt_remaining, bool &generated_eog) {
  LOG_INF("%s: Called with following arguments:\n", __func__);
  LOG_INF("- principal_id    = %s\n", principal_id.c_str());
  LOG_INF("- load_model_only = %s\n",
          std::format("{}", load_model_only).c_str());
  LOG_INF("- max_tokens      = %s\n", std::format("{}", max_tokens).c_str());

  common_params params;

  g_params = &params;

  if (!common_params_parse(argc, argv, params, LLAMA_EXAMPLE_MAIN,
                           print_usage)) {
    // ICPP-PATCH-START
    icpp_error_msg = "Error in common_params_parse.";
    // ICPP-PATCH-END
    return 1;
  }

  common_init();

  auto &sparams = params.sampling;

  // save choice to use color for later
  // (note for later: this is a slightly awkward choice)
  //icpp-patch console::init(params.simple_io, params.use_color);
  //icpp-patch atexit([]() { console::cleanup(); });

  if (params.logits_all) {
    LOG_ERR("************\n");
    LOG_ERR(
        "%s: please use the 'perplexity' tool for perplexity calculations\n",
        __func__);
    LOG_ERR("************\n\n");

    return 0;
  }

  if (params.embedding) {
    LOG_ERR("************\n");
    LOG_ERR("%s: please use the 'embedding' tool for embedding calculations\n",
            __func__);
    LOG_ERR("************\n\n");

    return 0;
  }

  if (params.n_ctx != 0 && params.n_ctx < 8) {
    LOG_WRN("%s: warning: minimum context size is 8, using minimum size.\n",
            __func__);
    params.n_ctx = 8;
  }

  if (params.rope_freq_base != 0.0) {
    LOG_WRN("%s: warning: changing RoPE frequency base to %g.\n", __func__,
            params.rope_freq_base);
  }

  if (params.rope_freq_scale != 0.0) {
    LOG_WRN("%s: warning: scaling RoPE frequency by %g.\n", __func__,
            params.rope_freq_scale);
  }

  LOG_INF("%s: llama backend init\n", __func__);

  llama_backend_init();
  llama_numa_init(params.numa);

  static llama_model *model = nullptr; // ICPP-PATCH: use static to preserve across calls
  llama_context *ctx;
  common_sampler *smpl = nullptr;

  // ICPP-PATCH-START
  // Don't give error if embd_inp = session_tokens. All is OK to just keep going
  bool embd_inp_is_session_tokens = false;

  // Keep track of the prompt portion not yet processed
  prompt_remaining.clear();

  g_model = &model;
  g_ctx = &ctx;
  g_smpl = &smpl;

  std::vector<common_chat_msg> chat_msgs;

  // load the model and apply lora adapter, if any
  LOG_INF("%s: load the model and apply lora adapter, if any\n", __func__);
  common_init_result llama_init = common_init_from_params(params);

  model = llama_init.model.get();
  ctx = llama_init.context.get();

  if (model == NULL) {
    LOG_ERR("%s: error: unable to load model\n", __func__);
    // ICPP-PATCH-START
    icpp_error_msg = std::format("{}: error: unable to load model)", __func__);
    // ICPP-PATCH-END
    return 1;
  }

  // ICPP-PATCH-START

  // Transfer the ownership of the model pointer. so it persists across calls in Orthogonal Persistence.
  // We manually take control over the memory management of the model pointer, using icpp_free_model() to free it.
  // NOTE: The release() method of std::unique_ptr relinquishes ownership of the managed
  //       object and returns the raw pointer to it.
  //       After the call to release(), the std::unique_ptr becomes empty
  //       (i.e., it no longer manages any object).
  model = llama_init.model.release();

  // Return if we are asked to ONLY load the model
  if (load_model_only) {
    return 0;
  }
  // ICPP-PATCH-END

  const llama_vocab *vocab = llama_model_get_vocab(model);
  auto chat_templates =
      common_chat_templates_from_model(model, params.chat_template);

  LOG_INF("%s: llama threadpool init, n_threads = %d\n", __func__,
          (int)params.cpuparams.n_threads);

  auto *reg = ggml_backend_dev_backend_reg(
      ggml_backend_dev_by_type(GGML_BACKEND_DEVICE_TYPE_CPU));
  auto *ggml_threadpool_new_fn =
      (decltype(ggml_threadpool_new) *)ggml_backend_reg_get_proc_address(
          reg, "ggml_threadpool_new");
  auto *ggml_threadpool_free_fn =
      (decltype(ggml_threadpool_free) *)ggml_backend_reg_get_proc_address(
          reg, "ggml_threadpool_free");

  // Validate function pointers before use
  if (!ggml_threadpool_new_fn || !ggml_threadpool_free_fn) {
    LOG_ERR("%s: failed to get threadpool function pointers\n", __func__);
    return 1;
  }

  struct ggml_threadpool_params tpp_batch =
      ggml_threadpool_params_from_cpu_params(params.cpuparams_batch);
  struct ggml_threadpool_params tpp =
      ggml_threadpool_params_from_cpu_params(params.cpuparams);

  // ICPP-PATCH-START
  // This is not supported in a canister
  // set_process_priority(params.cpuparams.priority);
  // ICPP-PATCH-END

  struct ggml_threadpool *threadpool_batch = NULL;
  if (!ggml_threadpool_params_match(&tpp, &tpp_batch)) {
    threadpool_batch = ggml_threadpool_new_fn(&tpp_batch);
    if (!threadpool_batch) {
      LOG_ERR("%s: batch threadpool create failed : n_threads %d\n", __func__,
              tpp_batch.n_threads);
      return 1;
    }

    // Start the non-batch threadpool in the paused state
    tpp.paused = true;
  }

  struct ggml_threadpool *threadpool = ggml_threadpool_new_fn(&tpp);
  if (!threadpool) {
    LOG_ERR("%s: threadpool create failed : n_threads %d\n", __func__,
            tpp.n_threads);
    return 1;
  }

  llama_attach_threadpool(ctx, threadpool, threadpool_batch);

  const int n_ctx_train = llama_model_n_ctx_train(model);
  const int n_ctx = llama_n_ctx(ctx);

  if (n_ctx > n_ctx_train) {
    LOG_WRN("%s: model was trained on only %d context tokens (%d specified)\n",
            __func__, n_ctx_train, n_ctx);
  }

  // auto enable conversation mode if chat template is available
  const bool has_chat_template =
      chat_templates.has_explicit_template && chat_templates.template_default;
  if (params.conversation_mode == COMMON_CONVERSATION_MODE_AUTO) {
    if (has_chat_template) {
      // ICPP-PATCH-START
      // conversation mode is not supported in a canister. Do not turn it on by default.
      // LOG_INF("%s: chat template is available, enabling conversation mode (disable it with -no-cnv)\n", __func__);
      // params.conversation_mode = COMMON_CONVERSATION_MODE_ENABLED;
      LOG_INF(
          "%s: chat template is available, but since canisters do not support conversation mode, we use -no-cnv by default.)\n",
          __func__);
      params.conversation_mode = COMMON_CONVERSATION_MODE_DISABLED;
      // ICPP-PATCH-END
    } else {
      params.conversation_mode = COMMON_CONVERSATION_MODE_DISABLED;
    }
  }

  // in case user force-activate conversation mode (via -cnv) without proper chat template, we show a warning
  if (params.conversation_mode && !has_chat_template) {
    LOG_WRN(
        "%s: chat template is not available or is not supported. This may cause the model to output suboptimal responses\n",
        __func__);
  }

  // print chat template example in conversation mode
  if (params.conversation_mode) {
    if (params.enable_chat_template) {
      LOG_INF("%s: chat template example:\n%s\n", __func__,
              common_chat_format_example(*chat_templates.template_default,
                                         params.use_jinja)
                  .c_str());
    } else {
      LOG_INF(
          "%s: in-suffix/prefix is specified, chat template will be disabled\n",
          __func__);
    }
  }

  // print system information
  {
    LOG_INF("\n");
    LOG_INF("%s\n", common_params_get_system_info(params).c_str());
    LOG_INF("\n");
  }

  std::string path_session = params.path_prompt_cache;
  // ICPP-PATCH-START
  // Each principal has their own cache folder
  std::string canister_path_session;
  if (!get_canister_path_session(path_session, principal_id,
                                 canister_path_session, icpp_error_msg)) {
    return 1;
  }
  path_session = canister_path_session;
  // ICPP-PATCH-END
  std::vector<llama_token> session_tokens;

  if (!path_session.empty()) {
    LOG_INF("%s: attempting to load saved session from '%s'\n", __func__,
            path_session.c_str());
    if (!file_exists(path_session)) {
      LOG_INF("%s: session file does not exist, will create.\n", __func__);
    } else if (file_is_empty(path_session)) {
      LOG_INF(
          "%s: The session file is empty. A new session will be initialized.\n",
          __func__);
    } else {
      // The file exists and is not empty
      session_tokens.resize(n_ctx);
      size_t n_token_count_out = 0;
      if (!llama_state_load_file(
              ctx, path_session.c_str(), session_tokens.data(),
              session_tokens.capacity(), &n_token_count_out)) {
        LOG_ERR("%s: failed to load session file '%s'\n", __func__,
                path_session.c_str());
        // ICPP-PATCH-START
        icpp_error_msg =
            std::format("{}: error: failed to load session file '{}')",
                        __func__, path_session.c_str());
        // ICPP-PATCH-END
        return 1;
      }
      session_tokens.resize(n_token_count_out);
      LOG_INF("%s: loaded a session with prompt size of %d tokens\n", __func__,
              (int)session_tokens.size());
    }
  }

  const bool add_bos = llama_vocab_get_add_bos(vocab);
  if (!llama_model_has_encoder(model)) {
    GGML_ASSERT(!llama_vocab_get_add_eos(vocab));
  }

  LOG_DBG("n_ctx: %d, add_bos: %d\n", n_ctx, add_bos);

  std::vector<llama_token> embd_inp;

  auto chat_add_and_format = [&chat_msgs,
                              &chat_templates](const std::string &role,
                                               const std::string &content) {
    common_chat_msg new_msg{role, content};
    auto formatted =
        common_chat_format_single(*chat_templates.template_default, chat_msgs,
                                  new_msg, role == "user", g_params->use_jinja);
    chat_msgs.push_back({role, content});
    LOG_DBG("formatted: '%s'\n", formatted.c_str());
    return formatted;
  };

  {
    auto prompt =
        (params.conversation_mode && params.enable_chat_template)
            // format the system prompt in conversation mode (fallback to default if empty)
            ? chat_add_and_format("system", params.prompt.empty()
                                                ? DEFAULT_SYSTEM_MESSAGE
                                                : params.prompt)
            // otherwise use the prompt as is
            : params.prompt;
    if (params.interactive_first || !params.prompt.empty() ||
        session_tokens.empty()) {
      LOG_DBG("tokenize the prompt\n");
      embd_inp = common_tokenize(ctx, prompt, true, true);
    } else {
      LOG_DBG("use session tokens\n");
      embd_inp = session_tokens;
      // ICPP-PATCH-START
      embd_inp_is_session_tokens = true;
      // ICPP-PATCH-END
    }

    // ICPP-PATCH-START
    if (prompt.size() > 0) {
      prompt_remaining = prompt;
    }
    // ICPP-PATCH-END

    LOG_DBG("prompt: \"%s\"\n", prompt.c_str());
    LOG_DBG("tokens: %s\n", string_from(ctx, embd_inp).c_str());
  }

  // Should not run without any tokens
  if (embd_inp.empty()) {
    if (add_bos) {
      embd_inp.push_back(llama_vocab_bos(vocab));
      LOG_WRN("embd_inp was considered empty and bos was added: %s\n",
              string_from(ctx, embd_inp).c_str());
    } else {
      LOG_ERR("input is empty\n");
      return -1;
    }
  }

  // Tokenize negative prompt
  // ICPP-PATCH-START
  // when the prompt is empty, then embd_inp = session_tokens, and all is OK to just keep going.
  if (!embd_inp_is_session_tokens) {
    // ICPP-PATCH-END
    if ((int)embd_inp.size() > n_ctx - 4) {
      LOG_ERR("%s: prompt is too long (%d tokens, max %d)\n", __func__,
              (int)embd_inp.size(), n_ctx - 4);
      // ICPP-PATCH-START
      icpp_error_msg =
          std::format("{}: error: prompt is too long ({} tokens, max {})",
                      __func__, (int)embd_inp.size(), n_ctx - 4);
      // ICPP-PATCH-END
      return 1;
    }
    // ICPP-PATCH-START
  }
  // ICPP-PATCH-END

  // debug message about similarity of saved session, if applicable
  size_t n_matching_session_tokens = 0;
  if (!session_tokens.empty()) {
    for (llama_token id : session_tokens) {
      if (n_matching_session_tokens >= embd_inp.size() ||
          id != embd_inp[n_matching_session_tokens]) {
        break;
      }
      n_matching_session_tokens++;
    }
    if (params.prompt.empty() && n_matching_session_tokens == embd_inp.size()) {
      LOG_INF("%s: using full prompt from session file\n", __func__);
    } else if (n_matching_session_tokens >= embd_inp.size()) {
      LOG_INF("%s: session file has exact match for prompt!\n", __func__);
    } else if (n_matching_session_tokens < (embd_inp.size() / 2)) {
      LOG_WRN(
          "%s: session file has low similarity to prompt (%zu / %zu tokens); will mostly be reevaluated\n",
          __func__, n_matching_session_tokens, embd_inp.size());
    } else {
      LOG_INF("%s: session file matches %zu / %zu tokens of prompt\n", __func__,
              n_matching_session_tokens, embd_inp.size());
    }

    // remove any "future" tokens that we might have inherited from the previous session
    llama_kv_cache_seq_rm(ctx, -1, n_matching_session_tokens, -1);
  }

  LOG_DBG(
      "recalculate the cached logits (check): embd_inp.size() %zu, n_matching_session_tokens %zu, embd_inp.size() %zu, session_tokens.size() %zu\n",
      embd_inp.size(), n_matching_session_tokens, embd_inp.size(),
      session_tokens.size());

  // if we will use the cache for the full prompt without reaching the end of the cache, force
  // reevaluation of the last token to recalculate the cached logits
  if (!embd_inp.empty() && n_matching_session_tokens == embd_inp.size() &&
      session_tokens.size() > embd_inp.size()) {
    LOG_DBG(
        "recalculate the cached logits (do): session_tokens.resize( %zu )\n",
        embd_inp.size() - 1);

    session_tokens.resize(embd_inp.size() - 1);
  }

  // number of tokens to keep when resetting context
  if (params.n_keep < 0 || params.n_keep > (int)embd_inp.size()) {
    params.n_keep = (int)embd_inp.size();
  } else {
    params.n_keep += add_bos; // always keep the BOS token
  }

  if (params.conversation_mode) {
    params.interactive_first = true;
  }

  // enable interactive mode if interactive start is specified
  if (params.interactive_first) {
    params.interactive = true;
  }

  if (params.verbose_prompt) {
    LOG_INF("%s: prompt: '%s'\n", __func__, params.prompt.c_str());
    LOG_INF("%s: number of tokens in prompt = %zu\n", __func__,
            embd_inp.size());
    for (int i = 0; i < (int)embd_inp.size(); i++) {
      LOG_INF("%6d -> '%s'\n", embd_inp[i],
              common_token_to_piece(ctx, embd_inp[i]).c_str());
    }

    if (params.n_keep > add_bos) {
      LOG_INF("%s: static prompt based on n_keep: '", __func__);
      for (int i = 0; i < params.n_keep; i++) {
        LOG_CNT("%s", common_token_to_piece(ctx, embd_inp[i]).c_str());
      }
      LOG_CNT("'\n");
    }
    LOG_INF("\n");
  }

  // ctrl+C handling
  //icpp-patch-begin
  //     {
  // #if defined (__unix__) || (defined (__APPLE__) && defined (__MACH__))
  //         struct sigaction sigint_action;
  //         sigint_action.sa_handler = sigint_handler;
  //         sigemptyset (&sigint_action.sa_mask);
  //         sigint_action.sa_flags = 0;
  //         sigaction(SIGINT, &sigint_action, NULL);
  // #elif defined (_WIN32)
  //         auto console_ctrl_handler = +[](DWORD ctrl_type) -> BOOL {
  //             return (ctrl_type == CTRL_C_EVENT) ? (sigint_handler(SIGINT), true) : false;
  //         };
  //         SetConsoleCtrlHandler(reinterpret_cast<PHANDLER_ROUTINE>(console_ctrl_handler), true);
  // #endif
  //     }
  //icpp-patch-end

  if (params.interactive) {
    LOG_INF("%s: interactive mode on.\n", __func__);

    if (!params.antiprompt.empty()) {
      for (const auto &antiprompt : params.antiprompt) {
        LOG_INF("Reverse prompt: '%s'\n", antiprompt.c_str());
        if (params.verbose_prompt) {
          auto tmp = common_tokenize(ctx, antiprompt, false, true);
          for (int i = 0; i < (int)tmp.size(); i++) {
            LOG_INF("%6d -> '%s'\n", tmp[i],
                    common_token_to_piece(ctx, tmp[i]).c_str());
          }
        }
      }
    }

    if (params.input_prefix_bos) {
      LOG_INF("Input prefix with BOS\n");
    }

    if (!params.input_prefix.empty()) {
      LOG_INF("Input prefix: '%s'\n", params.input_prefix.c_str());
      if (params.verbose_prompt) {
        auto tmp = common_tokenize(ctx, params.input_prefix, true, true);
        for (int i = 0; i < (int)tmp.size(); i++) {
          LOG_INF("%6d -> '%s'\n", tmp[i],
                  common_token_to_piece(ctx, tmp[i]).c_str());
        }
      }
    }

    if (!params.input_suffix.empty()) {
      LOG_INF("Input suffix: '%s'\n", params.input_suffix.c_str());
      if (params.verbose_prompt) {
        auto tmp = common_tokenize(ctx, params.input_suffix, false, true);
        for (int i = 0; i < (int)tmp.size(); i++) {
          LOG_INF("%6d -> '%s'\n", tmp[i],
                  common_token_to_piece(ctx, tmp[i]).c_str());
        }
      }
    }
  }

  smpl = common_sampler_init(model, sparams);
  if (!smpl) {
    LOG_ERR("%s: failed to initialize sampling subsystem\n", __func__);
    return 1;
  }

  LOG_INF("sampler seed: %u\n", common_sampler_get_seed(smpl));
  LOG_INF("sampler params: \n%s\n", sparams.print().c_str());
  LOG_INF("sampler chain: %s\n", common_sampler_print(smpl).c_str());

  LOG_INF("generate: n_ctx = %d, n_batch = %d, n_predict = %d, n_keep = %d\n",
          n_ctx, params.n_batch, params.n_predict, params.n_keep);

  // group-attention state
  // number of grouped KV tokens so far (used only if params.grp_attn_n > 1)
  int ga_i = 0;

  const int ga_n = params.grp_attn_n;
  const int ga_w = params.grp_attn_w;

  if (ga_n != 1) {
    GGML_ASSERT(ga_n > 0 && "grp_attn_n must be positive"); // NOLINT
    GGML_ASSERT(ga_w % ga_n == 0 &&
                "grp_attn_w must be a multiple of grp_attn_n"); // NOLINT
    //GGML_ASSERT(n_ctx_train % ga_w == 0     && "n_ctx_train must be a multiple of grp_attn_w");    // NOLINT
    //GGML_ASSERT(n_ctx >= n_ctx_train * ga_n && "n_ctx must be at least n_ctx_train * grp_attn_n"); // NOLINT
    LOG_INF("self-extend: n_ctx_train = %d, grp_attn_n = %d, grp_attn_w = %d\n",
            n_ctx_train, ga_n, ga_w);
  }
  LOG_INF("\n");

  if (params.interactive) {
    const char *control_message;
    if (params.multiline_input) {
      control_message =
          " - To return control to the AI, end your input with '\\'.\n"
          " - To return control without starting a new line, end your input with '/'.\n";
    } else {
      control_message =
          " - Press Return to return control to the AI.\n"
          " - To return control without starting a new line, end your input with '/'.\n"
          " - If you want to submit another line, end your input with '\\'.\n";
    }
    LOG_INF("== Running in interactive mode. ==\n");
#if defined(__unix__) || (defined(__APPLE__) && defined(__MACH__)) ||          \
    defined(_WIN32)
    LOG_INF(" - Press Ctrl+C to interject at any time.\n");
#endif
    LOG_INF("%s", control_message);
    if (params.conversation_mode && params.enable_chat_template &&
        params.prompt.empty()) {
      LOG_INF(
          " - Using default system message. To change it, set a different value via -p PROMPT or -f FILE argument.\n");
    }
    LOG_INF("\n");

    is_interacting = params.interactive_first;
  }

  bool is_antiprompt = false;
  bool input_echo = true;
  bool display = true;
  bool need_to_save_session =
      !path_session.empty() && n_matching_session_tokens < embd_inp.size();

  int n_past = 0;
  int n_remain = params.n_predict;
  int n_consumed = 0;
  int n_session_consumed = 0;

  // ICPP-PATCH-START
  // We can only handle max_tokens evaluations per call
  int n_eval_total = 0;
  // We break out of the while loop below a little bit different at end of generation
  // we actually first go back one more time, to store the eog token in the conversation & cache,
  // while llama.cpp does not do that
  // We do want to break the while loop cleanly, to go through the memory cleanup at the end
  generated_eog = false;
  bool break_while_loop = false;
  // ICPP-PATCH-END

  std::vector<int> input_tokens;
  g_input_tokens = &input_tokens;
  std::vector<int> output_tokens;
  g_output_tokens = &output_tokens;
  // std::ostringstream output_ss;     g_output_ss     = &output_ss; // ICPP_PATCH
  g_output_ss = &output_ss; // ICPP-PATCH: we pass this in via argument,
      //             so we can return it to canister caller
  std::ostringstream
      assistant_ss; // for storing current assistant message, used in conversation mode

  // the first thing we will do is to output the prompt, so set color accordingly
  // console::set_display(console::prompt);
  display = params.display_prompt;

  std::vector<llama_token> embd;

  // tokenized antiprompts
  std::vector<std::vector<llama_token>> antiprompt_ids;

  antiprompt_ids.reserve(params.antiprompt.size());
  for (const std::string &antiprompt : params.antiprompt) {
    antiprompt_ids.emplace_back(
        ::common_tokenize(ctx, antiprompt, false, true));
  }

  if (llama_model_has_encoder(model)) {
    int enc_input_size = embd_inp.size();
    llama_token *enc_input_buf = embd_inp.data();

    if (llama_encode(ctx, llama_batch_get_one(enc_input_buf, enc_input_size))) {
      LOG_ERR("%s : failed to eval\n", __func__);
      // ICPP-PATCH-START
      icpp_error_msg = std::format("{}: error: failed to eval (-1-)", __func__);
      // ICPP-PATCH-END
      return 1;
    }

    llama_token decoder_start_token_id = llama_model_decoder_start_token(model);
    if (decoder_start_token_id == LLAMA_TOKEN_NULL) {
      decoder_start_token_id = llama_vocab_bos(vocab);
    }

    embd_inp.clear();
    embd_inp.push_back(decoder_start_token_id);
  }

  while ((n_remain != 0 && !is_antiprompt) || params.interactive) {
    // predict
    if (!embd.empty()) {
      // Note: (n_ctx - 4) here is to match the logic for commandline prompt handling via
      // --prompt or --file which uses the same value.
      int max_embd_size = n_ctx - 4;

      // Ensure the input doesn't exceed the context size by truncating embd if necessary.
      if ((int)embd.size() > max_embd_size) {
        const int skipped_tokens = (int)embd.size() - max_embd_size;
        embd.resize(max_embd_size);

        // console::set_display(console::error);
        LOG_WRN("<<input too long: skipped %d token%s>>", skipped_tokens,
                skipped_tokens != 1 ? "s" : "");
        // console::set_display(console::reset);
      }

      if (ga_n == 1) {
        // infinite text generation via context shifting
        // if we run out of context:
        // - take the n_keep first tokens from the original prompt (via n_past)
        // - take half of the last (n_ctx - n_keep) tokens and recompute the logits in batches

        if (n_past + (int)embd.size() >= n_ctx) {
          if (!params.ctx_shift) {
            LOG_DBG(
                "\n\n%s: context full and context shift is disabled => stopping\n",
                __func__);
            break;
          }

          if (params.n_predict == -2) {
            LOG_DBG("\n\n%s: context full and n_predict == -%d => stopping\n",
                    __func__, params.n_predict);
            break;
          }

          const int n_left = n_past - params.n_keep;
          const int n_discard = n_left / 2;

          LOG_DBG(
              "context full, swapping: n_past = %d, n_left = %d, n_ctx = %d, n_keep = %d, n_discard = %d\n",
              n_past, n_left, n_ctx, params.n_keep, n_discard);

          llama_kv_cache_seq_rm(ctx, 0, params.n_keep,
                                params.n_keep + n_discard);
          llama_kv_cache_seq_add(ctx, 0, params.n_keep + n_discard, n_past,
                                 -n_discard);

          n_past -= n_discard;

          LOG_DBG("after swap: n_past = %d\n", n_past);

          LOG_DBG("embd: %s\n", string_from(ctx, embd).c_str());

          LOG_DBG("clear session path\n");
          path_session.clear();
        }
      } else {
        // context extension via Self-Extend
        while (n_past >= ga_i + ga_w) {
          const int ib = (ga_n * ga_i) / ga_w;
          const int bd = (ga_w / ga_n) * (ga_n - 1);
          const int dd = (ga_w / ga_n) - ib * bd - ga_w;

          LOG_DBG("\n");
          LOG_DBG("shift: [%6d, %6d] + %6d -> [%6d, %6d]\n", ga_i, n_past,
                  ib * bd, ga_i + ib * bd, n_past + ib * bd);
          LOG_DBG("div:   [%6d, %6d] / %6d -> [%6d, %6d]\n", ga_i + ib * bd,
                  ga_i + ib * bd + ga_w, ga_n, (ga_i + ib * bd) / ga_n,
                  (ga_i + ib * bd + ga_w) / ga_n);
          LOG_DBG("shift: [%6d, %6d] + %6d -> [%6d, %6d]\n",
                  ga_i + ib * bd + ga_w, n_past + ib * bd, dd,
                  ga_i + ib * bd + ga_w + dd, n_past + ib * bd + dd);

          llama_kv_cache_seq_add(ctx, 0, ga_i, n_past, ib * bd);
          llama_kv_cache_seq_div(ctx, 0, ga_i + ib * bd, ga_i + ib * bd + ga_w,
                                 ga_n);
          llama_kv_cache_seq_add(ctx, 0, ga_i + ib * bd + ga_w,
                                 n_past + ib * bd, dd);

          n_past -= bd;

          ga_i += ga_w / ga_n;

          LOG_DBG("\nn_past_old = %d, n_past = %d, ga_i = %d\n\n", n_past + bd,
                  n_past, ga_i);
        }
      }

      // try to reuse a matching prefix from the loaded session instead of re-eval (via n_past)
      if (n_session_consumed < (int)session_tokens.size()) {
        size_t i = 0;
        for (; i < embd.size(); i++) {
          if (embd[i] != session_tokens[n_session_consumed]) {
            session_tokens.resize(n_session_consumed);
            break;
          }

          n_past++;
          n_session_consumed++;

          // ICPP-PATCH-START
          // Keep track of the processed conversation tokens and the remaining prompt
          int id = embd[i];
          const std::string token_str =
              common_token_to_piece(ctx, id, params.special);
          conversation_ss << token_str;

          // if (prompt_remaining.find(token_str) == 0) {
          //     prompt_remaining.erase(0, token_str.length());
          // }
          // ICPP-PATCH-END

          if (n_session_consumed >= (int)session_tokens.size()) {
            ++i;
            break;
          }
        }
        if (i > 0) {
          embd.erase(embd.begin(), embd.begin() + i);
        }
      }

      for (int i = 0; i < (int)embd.size(); i += params.n_batch) {
        int n_eval = (int)embd.size() - i;
        if (n_eval > params.n_batch) {
          n_eval = params.n_batch;
        }

        // ICPP-PATCH-START
        // We must process the predictions in multiple calls due to IC's instruction limit
        if (max_tokens > 0 && n_eval >= max_tokens) {
          n_eval = max_tokens;
        }
        // ICPP-PATCH-END

        LOG_DBG("eval: %s\n", string_from(ctx, embd).c_str());

        if (llama_decode(ctx, llama_batch_get_one(&embd[i], n_eval))) {
          LOG_ERR("%s : failed to eval\n", __func__);
          // ICPP-PATCH-START
          icpp_error_msg =
              std::format("{}: error: failed to eval (-3-)", __func__);
          // ICPP-PATCH-END
          return 1;
        }

        n_past += n_eval;

        LOG_DBG("n_past = %d\n", n_past);
        // Display total tokens alongside total time
        if (params.n_print > 0 && n_past % params.n_print == 0) {
          LOG_DBG("\n\033[31mTokens consumed so far = %d / %d \033[0m\n",
                  n_past, n_ctx);
        }

        // ICPP-PATCH-START
        // Keep track of the processed conversation tokens and the remaining prompt
        for (int j = 0; j < n_eval; ++j) {
          int id = embd[i + j];
          const std::string token_str =
              common_token_to_piece(ctx, id, params.special);
          conversation_ss << token_str;

          // if (prompt_remaining.find(token_str) == 0) {
          //     prompt_remaining.erase(0, token_str.length());
          // }
        }

        // We break out of the while loop:
        // (-) if the last token was generated and it was eog
        // (-) if we generated max_tokens, to avoid running into IC's instruction limit
        //
        n_eval_total += n_eval;
        if (generated_eog || (max_tokens > 0 && n_eval_total >= max_tokens)) {
          if (!path_session.empty() && params.prompt_cache_all &&
              !params.prompt_cache_ro) {
            session_tokens.insert(session_tokens.end(), embd.begin(),
                                  embd.begin() + n_eval);
            n_session_consumed = session_tokens.size();
          }
          break_while_loop = true;
          break;
        }
        // ICPP-PATCH-END
      }
      // ICPP-PATCH-START
      if (break_while_loop) {
        break;
      }
      // ICPP-PATCH-END

      if (!embd.empty() && !path_session.empty()) {
        session_tokens.insert(session_tokens.end(), embd.begin(), embd.end());
        n_session_consumed = session_tokens.size();
      }
    }
    // ICPP-PATCH-START
    if (break_while_loop) {
      break;
    }
    // ICPP-PATCH-END

    embd.clear();

    if ((int)embd_inp.size() <= n_consumed && !is_interacting) {
      // optionally save the session on first sample (for faster prompt loading next time)
      if (!path_session.empty() && need_to_save_session &&
          !params.prompt_cache_ro) {
        // ICPP-PATCH-START
        std::string msg = "saving " + std::to_string(session_tokens.size()) +
                          " tokens to session file " + path_session + "\n";
        LOG_INF("%s", msg.c_str());
        // ICPP-PATCH-END
        need_to_save_session = false;
        llama_state_save_file(ctx, path_session.c_str(), session_tokens.data(),
                              session_tokens.size());

        LOG_DBG("saved session to %s\n", path_session.c_str());
      }

      const llama_token id = common_sampler_sample(smpl, ctx, -1);

      common_sampler_accept(smpl, id, /* accept_grammar= */ true);

      // LOG_DBG("last: %s\n", string_from(ctx, smpl->prev.to_vector()).c_str());

      embd.push_back(id);

      // echo this to console
      input_echo = true;

      // decrement remaining sampling budget
      --n_remain;

      LOG_DBG("n_remain: %d\n", n_remain);
    } else {
      // some user input remains from prompt or interaction, forward it to processing
      LOG_DBG("embd_inp.size(): %d, n_consumed: %d\n", (int)embd_inp.size(),
              n_consumed);
      while ((int)embd_inp.size() > n_consumed) {
        embd.push_back(embd_inp[n_consumed]);

        // push the prompt in the sampling context in order to apply repetition penalties later
        // for the prompt, we don't apply grammar rules
        common_sampler_accept(smpl, embd_inp[n_consumed],
                              /* accept_grammar= */ false);

        ++n_consumed;
        if ((int)embd.size() >= params.n_batch) {
          break;
        }

        // ICPP-PATCH-START
        if (max_tokens > 0 &&
            n_consumed >= n_matching_session_tokens + max_tokens) {
          std::ostringstream msg_stream;
          msg_stream << "ICPP is breaking the while loop -2- !" << std::endl;
          msg_stream << "- max_tokens                   = "
                     << std::to_string(max_tokens) << std::endl;
          msg_stream << "- n_consumed                   = "
                     << std::to_string(n_consumed) << std::endl;
          msg_stream << "- n_matching_session_tokens    = "
                     << std::to_string(n_matching_session_tokens) << std::endl;
          LOG_INF("%s", msg_stream.str().c_str());
          break;
        }
        // ICPP-PATCH-END
      }
    }

    // ICPP-PATCH-START
    std::string prompt_consumed = "";
    prompt_remaining.clear();
    int n_prompt_tokens_remaining = 0;
    size_t iii = 0;
    for (auto id : embd_inp) {
      const std::string token_str =
          common_token_to_piece(ctx, id, true); // include special tokens
      if (iii < n_consumed) {
        prompt_consumed += token_str;
      } else {
        ++n_prompt_tokens_remaining;
        prompt_remaining += token_str;
      }
      ++iii;
    }
    // std::cout << "llama_cpp: " << std::string(__func__) << " - " << "llama_cpp: " << std::string(__func__) << " - " << "prompt_consumed (" << n_consumed << " tokens) = " << prompt_consumed << std::endl;
    // std::cout << "llama_cpp: " << std::string(__func__) << " - " << "llama_cpp: " << std::string(__func__) << " - " << "prompt_remaining (" << n_prompt_tokens_remaining << " tokens) = "<< prompt_remaining << std::endl;
    // ICPP-PATCH-END

    // display text
    if (input_echo && display) {
      for (auto id : embd) {
        const std::string token_str =
            common_token_to_piece(ctx, id, params.special);

        // Console/Stream Output
        LOG("%s", token_str.c_str());

        // Record Displayed Tokens To Log
        // Note: Generated tokens are created one by one hence this check
        if (embd.size() > 1) {
          // Incoming Requested Tokens
          input_tokens.push_back(id);
        } else {
          // Outgoing Generated Tokens
          output_tokens.push_back(id);
          output_ss << token_str;
        }
      }
    }

    // reset color to default if there is no pending user input
    if (input_echo && (int)embd_inp.size() == n_consumed) {
      // console::set_display(console::reset);
      display = true;
    }

    // if not currently processing queued inputs;
    if ((int)embd_inp.size() <= n_consumed) {
      // check for reverse prompt in the last n_prev tokens
      if (!params.antiprompt.empty()) {
        const int n_prev = 32;
        const std::string last_output =
            common_sampler_prev_str(smpl, ctx, n_prev);

        is_antiprompt = false;
        // Check if each of the reverse prompts appears at the end of the output.
        // If we're not running interactively, the reverse prompt might be tokenized with some following characters
        // so we'll compensate for that by widening the search window a bit.
        for (std::string &antiprompt : params.antiprompt) {
          size_t extra_padding = params.interactive ? 0 : 2;
          size_t search_start_pos =
              last_output.length() >
                      static_cast<size_t>(antiprompt.length() + extra_padding)
                  ? last_output.length() -
                        static_cast<size_t>(antiprompt.length() + extra_padding)
                  : 0;

          if (last_output.find(antiprompt, search_start_pos) !=
              std::string::npos) {
            if (params.interactive) {
              is_interacting = true;
            }
            is_antiprompt = true;
            break;
          }
        }

        // check for reverse prompt using special tokens
        llama_token last_token = common_sampler_last(smpl);
        for (std::vector<llama_token> ids : antiprompt_ids) {
          if (ids.size() == 1 && last_token == ids[0]) {
            if (params.interactive) {
              is_interacting = true;
            }
            is_antiprompt = true;
            break;
          }
        }

        if (is_antiprompt) {
          LOG_DBG("found antiprompt: %s\n", last_output.c_str());
        }
      }

      // deal with end of generation tokens in interactive mode
      if (llama_vocab_is_eog(vocab, common_sampler_last(smpl))) {
        LOG_DBG("found an EOG token\n");

        if (params.interactive) {
          if (!params.antiprompt.empty()) {
            // tokenize and inject first reverse prompt
            const auto first_antiprompt =
                common_tokenize(ctx, params.antiprompt.front(), false, true);
            embd_inp.insert(embd_inp.end(), first_antiprompt.begin(),
                            first_antiprompt.end());
            is_antiprompt = true;
          }

          if (params.enable_chat_template) {
            chat_add_and_format("assistant", assistant_ss.str());
          }
          is_interacting = true;
          LOG("\n");
        }
      }

      // if current token is not EOG, we add it to current assistant message
      if (params.conversation_mode) {
        const auto id = common_sampler_last(smpl);
        assistant_ss << common_token_to_piece(ctx, id, false);
      }

      if (n_past > 0 && is_interacting) {
        LOG_DBG("waiting for user input\n");

        if (params.conversation_mode) {
          LOG("\n> ");
        }

        if (params.input_prefix_bos) {
          LOG_DBG("adding input prefix BOS token\n");
          embd_inp.push_back(llama_vocab_bos(vocab));
        }

        std::string buffer;
        if (!params.input_prefix.empty() && !params.conversation_mode) {
          LOG_DBG("appending input prefix: '%s'\n",
                  params.input_prefix.c_str());
          LOG("%s", params.input_prefix.c_str());
        }

        // color user input only
        // console::set_display(console::user_input);
        display = params.display_prompt;

        // std::string line;
        // bool another_line = true;
        // do {
        //     another_line = console::readline(line, params.multiline_input);
        //     buffer += line;
        // } while (another_line);

        // done taking input, reset color
        // console::set_display(console::reset);
        display = true;

        // Add tokens to embd only if the input buffer is non-empty
        // Entering a empty line lets the user pass control back
        if (buffer.length() > 1) {
          // append input suffix if any
          if (!params.input_suffix.empty() && !params.conversation_mode) {
            LOG_DBG("appending input suffix: '%s'\n",
                    params.input_suffix.c_str());
            LOG("%s", params.input_suffix.c_str());
          }

          LOG_DBG("buffer: '%s'\n", buffer.c_str());

          const size_t original_size = embd_inp.size();

          if (params.escape) {
            string_process_escapes(buffer);
          }

          bool format_chat =
              params.conversation_mode && params.enable_chat_template;
          std::string user_inp =
              format_chat ? chat_add_and_format("user", std::move(buffer))
                          : std::move(buffer);
          // TODO: one inconvenient of current chat template implementation is that we can't distinguish between user input and special tokens (prefix/postfix)
          const auto line_pfx =
              common_tokenize(ctx, params.input_prefix, false, true);
          const auto line_inp =
              common_tokenize(ctx, user_inp, false, format_chat);
          const auto line_sfx =
              common_tokenize(ctx, params.input_suffix, false, true);

          LOG_DBG("input tokens: %s\n", string_from(ctx, line_inp).c_str());

          // if user stop generation mid-way, we must add EOT to finish model's last response
          if (need_insert_eot && format_chat) {
            llama_token eot = llama_vocab_eot(vocab);
            embd_inp.push_back(eot == LLAMA_TOKEN_NULL ? llama_vocab_eos(vocab)
                                                       : eot);
            need_insert_eot = false;
          }

          embd_inp.insert(embd_inp.end(), line_pfx.begin(), line_pfx.end());
          embd_inp.insert(embd_inp.end(), line_inp.begin(), line_inp.end());
          embd_inp.insert(embd_inp.end(), line_sfx.begin(), line_sfx.end());

          for (size_t i = original_size; i < embd_inp.size(); ++i) {
            const llama_token token = embd_inp[i];
            output_tokens.push_back(token);
            output_ss << common_token_to_piece(ctx, token);
          }

          // reset assistant message
          assistant_ss.str("");

          n_remain -= line_inp.size();
          LOG_DBG("n_remain: %d\n", n_remain);
        } else {
          LOG_DBG("empty line, passing control back\n");
        }

        input_echo = false; // do not echo this again
      }

      if (n_past > 0) {
        if (is_interacting) {
          common_sampler_reset(smpl);
        }
        is_interacting = false;
      }
    }

    // ICPP-PATCH-START : do not set end-of-text with generated_eog if we're still processing inputs
    //                    the token might be an eog token like <im-end>, but it was just part of the prompt
    // if not currently processing queued inputs;
    if ((int)embd_inp.size() <= n_consumed) {
      // ICPP-PATCH-END

      // end of generation
      if (!embd.empty() && llama_vocab_is_eog(vocab, embd.back()) &&
          !(params.interactive)) {
        LOG(" [end of text]\n");
        // break;  // we do not break the loop here, but we do it above
        //           once the eog token has been decoded and added to conversation_ss & session_tokens
        // ICPP-PATCH-START
        generated_eog = true;
        // ICPP-PATCH-END
      }

      // ICPP-PATCH-START : do not set end-of-text with generated_eog if we're still processing inputs
    }
    // ICPP-PATCH-END

    // In interactive mode, respect the maximum number of tokens and drop back to user input when reached.
    // We skip this logic when n_predict == -1 (infinite) or -2 (stop at context size).
    if (params.interactive && n_remain <= 0 && params.n_predict >= 0) {
      n_remain = params.n_predict;
      is_interacting = true;
    }
  }

  if (!path_session.empty() && params.prompt_cache_all &&
      !params.prompt_cache_ro) {
    LOG("\n%s: saving final output to session file '%s'\n", __func__,
        path_session.c_str());
    llama_state_save_file(ctx, path_session.c_str(), session_tokens.data(),
                          session_tokens.size());
  }

  LOG("\n\n");
  common_perf_print(ctx, smpl);

  common_sampler_free(smpl);

  // ICPP-PATCH-START
  // Close log file and reset pointers, so next call will start fresh, with or without logging
  common_log_set_file(common_log_main(), nullptr);

  // Reset all static memory we do not want to carry over to the next update call
  reset_static_memory();
  // ICPP-PATCH-END

  llama_backend_free();

  ggml_threadpool_free_fn(threadpool);
  ggml_threadpool_free_fn(threadpool_batch);

  return 0;
}

// ICPP-PATCH-START:
// functions added for running on IC

// Function to be called by the canister to free the model which is persisted in Orthogonal Persisted memory
void icpp_free_model() {
  if (g_model && *g_model) {
    llama_model_free(*g_model);
    *g_model = nullptr;
    g_model = nullptr;
  }
}

void reset_static_memory() {
  /* Tip: to find what must be reset, use a native debug build and stop here 
            in lldb:
        
        lldb ./build-native/mockic.exe
        (lldb) breakpoint set --name reset_static_memory
        (lldb) run
        (lldb) target variable
    */

  // Avoid dangling pointers in static memory
  // -> The data pointed to is re-created each call
  // -> The data pointed to is cleared automatic, because:
  //    (-) it is a smart pointer (std::unique_ptr)
  //    (-) it is non-static

  g_ctx = nullptr;
  g_smpl = nullptr;
  g_params = nullptr;
  g_output_ss = nullptr;
  g_output_tokens = nullptr;
  g_input_tokens = nullptr;

  // Do not carry over any other values in static memory
  need_insert_eot = false;
  is_interacting = false;
}
// ICPP-PATCH-END
