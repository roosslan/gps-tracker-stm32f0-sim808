/*
 * Плата STM32F0DISCOVERY (STM32F051R8T6): тактирование, системное время,
 * сторожевой таймер, светодиоды и вывод PWRKEY модуля SIM808.
 *
 *   PA9  USART1_TX  -> SIM808 RXD
 *   PA10 USART1_RX  <- SIM808 TXD
 *   PA2  USART2_TX  -> RX USB-UART переходника (отладочный лог, необязательно)
 *   PB10 PWRKEY     -> SIM808 PWRKEY (открытый сток: тянем к земле или отпускаем)
 *   PC8  синий LED  — идёт обмен с модулем / ошибка
 *   PC9  зелёный LED — мигает редко: ищем спутники, часто: есть фикс
 */
#pragma once

#include <cstdint>

namespace board {

inline constexpr uint32_t kCpuHz = 48'000'000;

enum class ResetCause {
    Power,
    Pin,
    Watchdog,
    Software,
    Other,
};

void init();
ResetCause reset_cause();
const char *reset_cause_name(ResetCause cause);

uint32_t millis();
void delay_ms(uint32_t ms);
void watchdog_kick();
void wait_for_interrupt();

void led_blue(bool on);
void led_green(bool on);
void pwrkey(bool pressed);

} // namespace board
