#include "board.hpp"

#include "stm32f0xx.h"

// Частота ядра для функций CMSIS (SysTick_Config и др.). Объявлена в system_stm32f0xx.h.
extern "C" uint32_t SystemCoreClock;
uint32_t SystemCoreClock = board::kCpuHz;

namespace {

volatile uint32_t ticks_ms;
board::ResetCause cause = board::ResetCause::Other;

/*
 * Разгоняет ядро до 48 МГц — максимума для STM32F051. Источник — встроенный
 * RC-генератор HSI 8 МГц: делится на 2 и умножается PLL на 12, так что
 * кварц на плате не нужен. Перед переключением flash переводится на
 * 1 такт ожидания с предвыборкой, иначе на 48 МГц она не успевает.
 * Шины AHB и APB работают без делителя, на 48 МГц.
 * Параметров нет.
 */
void clock_init()
{
    FLASH->ACR = FLASH_ACR_PRFTBE | FLASH_ACR_LATENCY;

    RCC->CFGR = (RCC->CFGR & ~(RCC_CFGR_PLLSRC | RCC_CFGR_PLLMUL | RCC_CFGR_HPRE | RCC_CFGR_PPRE))
              | RCC_CFGR_PLLSRC_HSI_DIV2 | RCC_CFGR_PLLMUL12;
    RCC->CR |= RCC_CR_PLLON;
    while (!(RCC->CR & RCC_CR_PLLRDY)) {
    }
    RCC->CFGR = (RCC->CFGR & ~RCC_CFGR_SW) | RCC_CFGR_SW_PLL;
    while ((RCC->CFGR & RCC_CFGR_SWS) != RCC_CFGR_SWS_PLL) {
    }
}

/*
 * Читает флаги причины сброса из RCC->CSR, запоминает самую важную
 * для reset_cause() и сбрасывает флаги, чтобы при следующем старте
 * они показывали только новую причину. При включении питания заодно
 * выставляется и флаг сброса по выводу NRST, поэтому флаги проверяются
 * по порядку: сторожевой таймер, программный сброс, питание, кнопка.
 * Параметров нет.
 */
void read_reset_cause()
{
    uint32_t csr = RCC->CSR;
    if (csr & RCC_CSR_IWDGRSTF)
        cause = board::ResetCause::Watchdog;
    else if (csr & RCC_CSR_SFTRSTF)
        cause = board::ResetCause::Software;
    else if (csr & (RCC_CSR_PORRSTF | RCC_CSR_V18PWRRSTF))
        cause = board::ResetCause::Power;
    else if (csr & RCC_CSR_PINRSTF)
        cause = board::ResetCause::Pin;
    else
        cause = board::ResetCause::Other;
    RCC->CSR |= RCC_CSR_RMVF;
}

/*
 * Запускает независимый сторожевой таймер IWDG: если программа не вызовет
 * watchdog_kick() дольше ~6.5 с, плата перезагрузится. Время
 * складывается так: LSI ~40 кГц / 64 (предделитель) * 4096 (перезагрузка).
 * Самая долгая блокирующая операция — отправка SMS до 60 с — кормит
 * таймер изнутри цикла ожидания. Остановить запущенный IWDG нельзя
 * до следующего сброса.
 * Параметров нет.
 */
void watchdog_init()
{
    IWDG->KR = 0xCCCC;
    IWDG->KR = 0x5555;
    IWDG->PR = 4;
    IWDG->RLR = 0xFFF;
    while (IWDG->SR) {
    }
    IWDG->KR = 0xAAAA;
}

/*
 * Настраивает выводы платы:
 *   PC8, PC9 — выходы push-pull на синий и зелёный светодиоды;
 *   PB10     — выход с открытым стоком на PWRKEY модуля SIM808, сразу
 *              «отпущен» (высокий импеданс). Вывод выдерживает 5 В, а PWRKEY
 *              внутри SIM808 подтянут к VBAT (~4 В), поэтому push-pull
 *              нельзя: он выдавал бы 3.3 В против подтяжки.
 * Выводы UART настраиваются отдельно в uart.cpp.
 * Параметров нет.
 */
void gpio_init()
{
    RCC->AHBENR |= RCC_AHBENR_GPIOBEN | RCC_AHBENR_GPIOCEN;

    GPIOC->MODER = (GPIOC->MODER & ~(GPIO_MODER_MODER8 | GPIO_MODER_MODER9))
                 | GPIO_MODER_MODER8_0 | GPIO_MODER_MODER9_0;

    GPIOB->BSRR = GPIO_BSRR_BS_10;
    GPIOB->OTYPER |= GPIO_OTYPER_OT_10;
    GPIOB->MODER = (GPIOB->MODER & ~GPIO_MODER_MODER10) | GPIO_MODER_MODER10_0;
}

} // namespace

/*
 * Прерывание системного таймера SysTick, приходит раз в 1 мс (настраивается
 * в board::init). Увеличивает счётчик миллисекунд, который возвращает
 * board::millis(). extern "C" — чтобы имя совпало с таблицей векторов
 * в startup.cpp. Вызывается аппаратно, из кода не вызывать.
 */
extern "C" void SysTick_Handler()
{
    ticks_ms = ticks_ms + 1;
}

namespace board {

/*
 * Полная начальная настройка платы: запоминает причину сброса, включает
 * такты 48 МГц, запускает SysTick на 1 мс, настраивает светодиоды и PWRKEY
 * и включает сторожевой таймер. Вызывать один раз, первой строкой main().
 * Параметров нет.
 */
void init()
{
    read_reset_cause();
    clock_init();
    SysTick_Config(kCpuHz / 1000u);
    gpio_init();
    watchdog_init();
}

/*
 * Сообщает, почему плата стартовала: включение питания, кнопка RESET,
 * сторожевой таймер или программный сброс. Причина считывается один раз
 * в init().
 * Параметров нет.
 *
 * Возвращает: причину последнего сброса.
 */
ResetCause reset_cause()
{
    return cause;
}

/*
 * Переводит причину сброса в короткое название для лога.
 *
 * Параметры:
 *   c — причина сброса из reset_cause().
 *
 * Возвращает: строку-константу, например "power-on" или "WATCHDOG".
 */
const char *reset_cause_name(ResetCause c)
{
    switch (c) {
    case ResetCause::Power:    return "power-on";
    case ResetCause::Pin:      return "reset button";
    case ResetCause::Watchdog: return "WATCHDOG";
    case ResetCause::Software: return "software";
    default:                   return "other";
    }
}

/*
 * Сколько миллисекунд прошло с init(). Счётчик 32-битный и переполняется
 * примерно через 49.7 суток, поэтому интервалы нужно считать вычитанием:
 * (millis() - start) >= timeout — такая запись остаётся верной и при
 * переполнении.
 * Параметров нет.
 *
 * Возвращает: время в миллисекундах.
 */
uint32_t millis()
{
    return ticks_ms;
}

/*
 * Сбрасывает сторожевой таймер, давая программе ещё ~6.5 с. Вызывать
 * в главном цикле и в каждом длинном цикле ожидания.
 * Параметров нет.
 */
void watchdog_kick()
{
    IWDG->KR = 0xAAAA;
}

/*
 * Усыпляет ядро (инструкция WFI) до ближайшего прерывания: тика SysTick
 * (не дольше 1 мс) или байта из UART. Экономит питание в циклах ожидания.
 * Параметров нет.
 */
void wait_for_interrupt()
{
    __WFI();
}

/*
 * Блокирующая пауза. Во время ожидания кормит сторожевой таймер и спит
 * на WFI до следующего прерывания, а байты UART продолжают копиться в буфере.
 *
 * Параметры:
 *   ms — длительность паузы в миллисекундах; реальная пауза может быть
 *        длиннее на время до ближайшего тика SysTick, то есть максимум на 1 мс.
 */
void delay_ms(uint32_t ms)
{
    uint32_t start = ticks_ms;
    while (ticks_ms - start < ms) {
        watchdog_kick();
        __WFI();
    }
}

/*
 * Включает или выключает синий светодиод LD4 (PC8).
 *
 * Параметры:
 *   on — true: зажечь, false: погасить.
 */
void led_blue(bool on)
{
    GPIOC->BSRR = on ? GPIO_BSRR_BS_8 : GPIO_BSRR_BR_8;
}

/*
 * Включает или выключает зелёный светодиод LD3 (PC9).
 *
 * Параметры:
 *   on — true: зажечь, false: погасить.
 */
void led_green(bool on)
{
    GPIOC->BSRR = on ? GPIO_BSRR_BS_9 : GPIO_BSRR_BR_9;
}

/*
 * Управляет выводом PWRKEY модуля SIM808 (PB10, открытый сток). Удержание
 * PWRKEY у земли больше 1 с включает выключенный модуль и выключает
 * включённый, поэтому сначала стоит проверить, отвечает ли модуль
 * (см. sim808.cpp).
 *
 * Параметры:
 *   pressed — true: прижать PWRKEY к земле («нажать кнопку»),
 *             false: отпустить (модуль сам подтягивает вывод к VBAT).
 */
void pwrkey(bool pressed)
{
    GPIOB->BSRR = pressed ? GPIO_BSRR_BR_10 : GPIO_BSRR_BS_10;
}

} // namespace board
