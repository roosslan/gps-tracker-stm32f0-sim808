/*
 * Разбор SMS-строк SIM808 в текстовом режиме (AT+CMGF=1) и команд от пользователя.
 * Чистая логика без железа — собирается и тестируется на ПК.
 */
#pragma once

#include <cstddef>
#include <optional>
#include <span>
#include <string_view>

#include "text.hpp"

namespace sms {

inline constexpr size_t kNumberMax = 24;  // с '\0'
inline constexpr size_t kTextMax   = 160; // одна SMS в кодировке GSM 7-bit

using Number = Text<kNumberMax>;
using Body   = Text<kTextMax + 1>;

enum class Command {
    None, // не команда — не отвечаем, чтобы не спорить с рассылками оператора
    Where,
    Status,
    Help,
};

std::optional<int> parse_cmti(std::string_view line);
std::optional<int> parse_cmgl(std::string_view line);
std::optional<std::string_view> parse_cmgr_sender(std::string_view line);
Command parse_command(std::string_view text);
bool sender_allowed(std::string_view number, std::span<const char *const> allowed);

} // namespace sms
