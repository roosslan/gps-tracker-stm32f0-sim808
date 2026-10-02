/*
 * USART1 — связь с SIM808 (приём по прерыванию в кольцевой буфер).
 * USART2 — отладочный лог, только передача.
 */
#pragma once

#include <optional>
#include <string_view>

#include "text.hpp"

namespace sim_uart {

void init(unsigned baud);
void put(char c);
void write(std::string_view s);
std::optional<char> get();
unsigned dropped();

} // namespace sim_uart

namespace logger {

void init(unsigned baud);
void print(const char *fmt, ...) TEXT_PRINTF_FORMAT(1, 2);

} // namespace logger
