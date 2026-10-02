// Таблица векторов и старт для STM32F051x8 (Cortex-M0).
#include <cstdint>

int main(); // main не объявляют внутри extern "C": у неё особая привязка

extern "C" {

extern uint32_t _sidata, _sdata, _edata, _sbss, _ebss, _estack;

void __libc_init_array();

/*
 * Первая функция после сброса или включения питания (адрес берётся из
 * второго слова таблицы векторов). Стек к этому моменту уже указывает
 * на конец RAM — ядро само берёт его из первого слова таблицы. Функция:
 *   1. копирует начальные значения инициализированных глобальных переменных
 *      (.data) из flash в RAM;
 *   2. обнуляет неинициализированные глобальные переменные (.bss);
 *   3. вызывает конструкторы глобальных объектов C++ (__libc_init_array);
 *   4. вызывает main(). Если main() вдруг вернётся, зависает, и сторожевой
 *      таймер перезагрузит плату.
 * Параметров нет.
 */
void Reset_Handler()
{
    uint32_t *src = &_sidata;
    for (uint32_t *dst = &_sdata; dst < &_edata;)
        *dst++ = *src++;
    for (uint32_t *dst = &_sbss; dst < &_ebss;)
        *dst++ = 0;
    __libc_init_array();
    main();
    for (;;) {
    }
}

/*
 * Обработчик для всех прерываний, у которых нет своей функции, и для
 * HardFault. Просто зависает в бесконечном цикле: если сторожевой таймер
 * уже запущен, через ~6.5 с плата перезагрузится с причиной WATCHDOG,
 * а в отладчике видно, что программа застряла здесь.
 * Параметров нет.
 */
void Default_Handler()
{
    for (;;) {
    }
}

#define WEAK_ALIAS __attribute__((weak, alias("Default_Handler")))

void NMI_Handler() WEAK_ALIAS;
void HardFault_Handler() WEAK_ALIAS;
void SVC_Handler() WEAK_ALIAS;
void PendSV_Handler() WEAK_ALIAS;
void SysTick_Handler() WEAK_ALIAS;
void WWDG_IRQHandler() WEAK_ALIAS;
void PVD_IRQHandler() WEAK_ALIAS;
void RTC_IRQHandler() WEAK_ALIAS;
void FLASH_IRQHandler() WEAK_ALIAS;
void RCC_IRQHandler() WEAK_ALIAS;
void EXTI0_1_IRQHandler() WEAK_ALIAS;
void EXTI2_3_IRQHandler() WEAK_ALIAS;
void EXTI4_15_IRQHandler() WEAK_ALIAS;
void TSC_IRQHandler() WEAK_ALIAS;
void DMA1_Channel1_IRQHandler() WEAK_ALIAS;
void DMA1_Channel2_3_IRQHandler() WEAK_ALIAS;
void DMA1_Channel4_5_IRQHandler() WEAK_ALIAS;
void ADC1_COMP_IRQHandler() WEAK_ALIAS;
void TIM1_BRK_UP_TRG_COM_IRQHandler() WEAK_ALIAS;
void TIM1_CC_IRQHandler() WEAK_ALIAS;
void TIM2_IRQHandler() WEAK_ALIAS;
void TIM3_IRQHandler() WEAK_ALIAS;
void TIM6_DAC_IRQHandler() WEAK_ALIAS;
void TIM14_IRQHandler() WEAK_ALIAS;
void TIM15_IRQHandler() WEAK_ALIAS;
void TIM16_IRQHandler() WEAK_ALIAS;
void TIM17_IRQHandler() WEAK_ALIAS;
void I2C1_IRQHandler() WEAK_ALIAS;
void I2C2_IRQHandler() WEAK_ALIAS;
void SPI1_IRQHandler() WEAK_ALIAS;
void SPI2_IRQHandler() WEAK_ALIAS;
void USART1_IRQHandler() WEAK_ALIAS;
void USART2_IRQHandler() WEAK_ALIAS;
void CEC_CAN_IRQHandler() WEAK_ALIAS;

using vector_t = void (*)();

// Порядок — из IRQn_Type в stm32f051x8.h; nullptr — зарезервированные позиции.
// extern нужен, чтобы const-массив получил внешнюю связь и не был выброшен.
extern const vector_t vector_table[16 + 32];
__attribute__((section(".isr_vector"), used))
const vector_t vector_table[16 + 32] = {
    reinterpret_cast<vector_t>(&_estack),
    Reset_Handler,
    NMI_Handler,
    HardFault_Handler,
    nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
    SVC_Handler,
    nullptr, nullptr,
    PendSV_Handler,
    SysTick_Handler,
    WWDG_IRQHandler,                // IRQ 0
    PVD_IRQHandler,                 // 1
    RTC_IRQHandler,                 // 2
    FLASH_IRQHandler,               // 3
    RCC_IRQHandler,                 // 4
    EXTI0_1_IRQHandler,             // 5
    EXTI2_3_IRQHandler,             // 6
    EXTI4_15_IRQHandler,            // 7
    TSC_IRQHandler,                 // 8
    DMA1_Channel1_IRQHandler,       // 9
    DMA1_Channel2_3_IRQHandler,     // 10
    DMA1_Channel4_5_IRQHandler,     // 11
    ADC1_COMP_IRQHandler,           // 12
    TIM1_BRK_UP_TRG_COM_IRQHandler, // 13
    TIM1_CC_IRQHandler,             // 14
    TIM2_IRQHandler,                // 15
    TIM3_IRQHandler,                // 16
    TIM6_DAC_IRQHandler,            // 17
    nullptr,                        // 18
    TIM14_IRQHandler,               // 19
    TIM15_IRQHandler,               // 20
    TIM16_IRQHandler,               // 21
    TIM17_IRQHandler,               // 22
    I2C1_IRQHandler,                // 23
    I2C2_IRQHandler,                // 24
    SPI1_IRQHandler,                // 25
    SPI2_IRQHandler,                // 26
    USART1_IRQHandler,              // 27
    USART2_IRQHandler,              // 28
    nullptr,                        // 29
    CEC_CAN_IRQHandler,             // 30
    nullptr,                        // 31
};

} // extern "C"
