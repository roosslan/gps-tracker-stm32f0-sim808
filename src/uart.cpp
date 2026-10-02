#include "uart.hpp"

#include <cstdarg>
#include <cstdint>

#include "board.hpp"
#include "ring_buffer.hpp"
#include "stm32f0xx.h"

namespace {

RingBuffer<char, 512> rx;
volatile unsigned rx_dropped;
bool log_ready;

/*
 * Переключает вывод порта A в режим альтернативной функции AF1. На STM32F051
 * это USART1 (PA9, PA10) и USART2 (PA2, PA3). Тактирование GPIOA к этому
 * моменту должно быть включено.
 *
 * Параметры:
 *   pin — номер вывода порта A, 0..15.
 */
void pin_af1(unsigned pin)
{
    GPIOA->MODER = (GPIOA->MODER & ~(3u << (pin * 2))) | (2u << (pin * 2));
    volatile uint32_t &afr = GPIOA->AFR[pin / 8];
    unsigned shift = (pin % 8) * 4;
    afr = (afr & ~(0xFu << shift)) | (1u << shift);
}

/*
 * Считает значение регистра BRR для заданной скорости с округлением
 * до ближайшего целого (USART тактируется от PCLK = 48 МГц).
 *
 * Параметры:
 *   baud — скорость, бит/с.
 *
 * Возвращает: делитель для USARTx->BRR (417 для 115200).
 */
uint32_t brr_for(unsigned baud)
{
    return (board::kCpuHz + baud / 2) / baud;
}

/*
 * Отправляет один байт в отладочный USART2. Блокирует, пока освободится
 * передатчик.
 *
 * Параметры:
 *   c — байт для отправки.
 */
void log_put(char c)
{
    while (!(USART2->ISR & USART_ISR_TXE)) {
    }
    USART2->TDR = static_cast<uint8_t>(c);
}

} // namespace

/*
 * Прерывание USART1. Забирает принятый байт в кольцевой буфер и сбрасывает
 * флаги ошибок (переполнение, ошибка кадра, шум). Если буфер полон или была
 * ошибка, байт считается потерянным и учитывается в sim_uart::dropped().
 * extern "C" — чтобы имя совпало с таблицей векторов в startup.cpp.
 * Вызывается аппаратно, из кода не вызывать.
 */
extern "C" void USART1_IRQHandler()
{
    uint32_t isr = USART1->ISR;
    if (isr & (USART_ISR_ORE | USART_ISR_FE | USART_ISR_NE)) {
        USART1->ICR = USART_ICR_ORECF | USART_ICR_FECF | USART_ICR_NCF;
        rx_dropped = rx_dropped + 1;
    }
    if (isr & USART_ISR_RXNE) {
        char c = static_cast<char>(USART1->RDR);
        if (!rx.push(c))
            rx_dropped = rx_dropped + 1;
    }
}

namespace sim_uart {

/*
 * Настраивает USART1 для связи с SIM808: PA9 — TX, PA10 — RX (с подтяжкой
 * вверх, чтобы оторванный провод не давал мусор), формат 8N1. Каждый
 * принятый байт прерывание кладёт в кольцевой буфер на 511 байт. Детектор
 * переполнения выключен: при переполнении теряется байт, но UART не
 * блокируется.
 *
 * Параметры:
 *   baud — скорость, бит/с (обычно 115200). SIM808 в режиме autobaud сам
 *          подстраивается под неё после первых команд "AT".
 */
void init(unsigned baud)
{
    RCC->AHBENR |= RCC_AHBENR_GPIOAEN;
    RCC->APB2ENR |= RCC_APB2ENR_USART1EN;

    pin_af1(9);
    pin_af1(10);
    GPIOA->PUPDR = (GPIOA->PUPDR & ~GPIO_PUPDR_PUPDR10) | GPIO_PUPDR_PUPDR10_0;

    USART1->CR1 = 0;
    USART1->BRR = brr_for(baud);
    USART1->CR3 = USART_CR3_OVRDIS;
    USART1->CR1 = USART_CR1_TE | USART_CR1_RE | USART_CR1_RXNEIE | USART_CR1_UE;

    NVIC_SetPriority(USART1_IRQn, 1);
    NVIC_EnableIRQ(USART1_IRQn);
}

/*
 * Достаёт очередной принятый от модуля байт из кольцевого буфера.
 * Не блокирует. Вызывать только из основного кода, не из прерываний.
 * Параметров нет.
 *
 * Возвращает: байт или std::nullopt, если буфер пуст.
 */
std::optional<char> get()
{
    return rx.pop();
}

/*
 * Сколько принятых байт потеряно с момента старта: не хватило места
 * в кольцевом буфере или UART зафиксировал ошибку приёма. Растущее значение
 * означает, что главный цикл слишком долго не вызывает at::poll(), или
 * проблему с проводами.
 * Параметров нет.
 *
 * Возвращает: число потерянных байт.
 */
unsigned dropped()
{
    return rx_dropped;
}

/*
 * Отправляет один байт модулю. Блокирует, пока освободится передатчик:
 * на 115200 бит/с это около 87 мкс на байт.
 *
 * Параметры:
 *   c — байт для отправки (включая управляющие: Ctrl+Z = 0x1A, ESC = 0x1B).
 */
void put(char c)
{
    while (!(USART1->ISR & USART_ISR_TXE)) {
    }
    USART1->TDR = static_cast<uint8_t>(c);
}

/*
 * Отправляет модулю строку побайтно, блокирующе. '\r' в конце не добавляет.
 *
 * Параметры:
 *   s — отправляемые байты.
 */
void write(std::string_view s)
{
    for (char c : s)
        put(c);
}

} // namespace sim_uart

namespace logger {

/*
 * Настраивает USART2 только на передачу для отладочного лога: PA2 — TX,
 * формат 8N1. Приёма нет, вывод PA3 не трогается. До вызова этой функции
 * print() ничего не делает.
 *
 * Параметры:
 *   baud — скорость, бит/с; на USB-UART переходнике нужно выставить такую же.
 */
void init(unsigned baud)
{
    RCC->AHBENR |= RCC_AHBENR_GPIOAEN;
    RCC->APB1ENR |= RCC_APB1ENR_USART2EN;
    pin_af1(2);
    USART2->BRR = brr_for(baud);
    USART2->CR1 = USART_CR1_TE | USART_CR1_UE;
    log_ready = true;
}

/*
 * Пишет строку в отладочный лог (USART2) с меткой времени "[секунды.мс] "
 * в начале и "\r\n" в конце. Внутренние '\n' заменяются на "\r\n", чтобы
 * терминал не выводил многострочный текст «лесенкой». Текст длиннее
 * 199 символов обрезается. Передача блокирующая: строка на 80 символов
 * занимает около 7 мс. Если init() не вызывалась, ничего не делает.
 *
 * Параметры:
 *   fmt — формат printf (без float: newlib-nano собран без него);
 *   ... — аргументы формата.
 */
void print(const char *fmt, ...)
{
    if (!log_ready)
        return;

    Text<216> line;
    uint32_t t = board::millis();
    line.appendf("[%5lu.%03lu] ", static_cast<unsigned long>(t / 1000), static_cast<unsigned long>(t % 1000));
    va_list ap;
    va_start(ap, fmt);
    line.vappendf(fmt, ap);
    va_end(ap);

    for (char c : line.view()) {
        if (c == '\n')
            log_put('\r');
        log_put(c);
    }
    log_put('\r');
    log_put('\n');
}

} // namespace logger
