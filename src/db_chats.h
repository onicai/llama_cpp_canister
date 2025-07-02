#pragma once

#include "wasm_symbol.h"
#include <string>

void chats_resume() WASM_SYMBOL_EXPORTED("canister_update chats_resume");
void chats_pause() WASM_SYMBOL_EXPORTED("canister_update chats_pause");
void get_chats() WASM_SYMBOL_EXPORTED("canister_query get_chats");

bool is_db_chats_active();

bool get_saved_chats_dir(const std::string &principal_id,
                         std::string &saved_chats_dir, std::string &error_msg);

bool db_chats_new(const std::string &principal_id, std::string &error_msg);

bool db_chats_clean(const std::string &principal_id, std::string &error_msg);

bool db_chats_save_conversation(const std::string &conversation,
                                const std::string &principal_id,
                                std::string &error_msg);
