/*
 * Поддельное железо для тестов AT-движка и команд SIM808 на ПК.
 *
 * Вместо board.cpp и uart.cpp тест линкуется с fake_hw.cpp: время идёт
 * само (каждый вызов board::millis() прибавляет 1 мс), а «модуль» отвечает
 * на AT-команды заготовленными строками. Так настоящие at.cpp и sim808.cpp
 * проверяются без платы, вместе с таймаутами.
 */
#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace fake {

void reset();
void respond(const std::string &command, const std::string &reply);
void unsolicited(const std::string &bytes);
const std::string &sent();
bool log_contains(std::string_view text);
int pwrkey_presses();

} // namespace fake
