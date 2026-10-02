/*
 * AT-движок: собирает строки из UART, отличает ответ на текущую команду
 * от незапрошенных сообщений модуля (URC) и запоминает индексы новых SMS.
 */
#pragma once

#include <cstdint>
#include <optional>
#include <string_view>

#include "text.hpp"

namespace at {

enum class Result {
    Ok,
    Error,
    Timeout,
};

void poll();

Result cmd(std::string_view command, uint32_t timeout_ms, TextBuffer *resp = nullptr);
bool query(std::string_view command, std::string_view prefix, TextBuffer &line, uint32_t timeout_ms);

bool cmd_prompt(std::string_view command, uint32_t timeout_ms);
void write(std::string_view s);
void write(char c);
Result wait_result(uint32_t timeout_ms);

std::string_view last_error();
std::optional<int> pop_sms();
bool take_power_down();

} // namespace at
