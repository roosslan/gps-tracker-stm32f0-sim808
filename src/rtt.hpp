/*
 * Отладочный лог через ST-LINK по протоколу SEGGER RTT.
 *
 * В RAM лежит управляющий блок с меткой "SEGGER RTT" и кольцевой буфер.
 * Прошивка дописывает в буфер текст, а OpenOCD через SWD находит блок
 * по метке и забирает текст, пока прошивка работает, не останавливая её.
 * Отдельный USB-UART переходник не нужен.
 *
 * Формат блока совместим с SEGGER RTT, поэтому лог читают OpenOCD, pyOCD,
 * probe-rs и J-Link. Если программатор не подключён, буфер заполняется
 * и новые строки отбрасываются — прошивка при этом не ждёт и не тормозит.
 */
#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace rtt {

inline constexpr size_t kUpBufferSize = 1024;  // прошивка -> ПК
inline constexpr size_t kDownBufferSize = 16;  // ПК -> прошивка, не используется

// Поля и порядок задаёт протокол RTT: программатор читает их по смещениям.
struct Buffer {
    const char *name;
    char *data;
    uint32_t size;
    volatile uint32_t write_offset; // двигает писатель
    volatile uint32_t read_offset;  // двигает читатель
    uint32_t flags;                 // режим при переполнении: 1 = обрезать
};

struct ControlBlock {
    char id[16]; // "SEGGER RTT" и нули — по этой метке блок ищут в RAM
    int32_t max_up_buffers;
    int32_t max_down_buffers;
    Buffer up[1];
    Buffer down[1];
};

void init();
size_t write(std::string_view s);

} // namespace rtt

// Имя как у SEGGER: некоторые программы ищут блок не по метке, а по символу.
extern "C" rtt::ControlBlock _SEGGER_RTT;
