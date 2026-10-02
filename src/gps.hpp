/*
 * Разбор ответа SIM808 на AT+CGNSINF.
 *
 *   +CGNSINF: <run>,<fix>,<UTC yyyyMMddhhmmss.sss>,<lat>,<lon>,<alt>,<speed km/h>,
 *             <course>,<fix mode>,<res>,<HDOP>,<PDOP>,<VDOP>,<res>,
 *             <GPS sats in view>,<GNSS sats used>,<GLONASS sats in view>,...
 *
 * У STM32F0 нет FPU, поэтому координаты хранятся целыми числами
 * в миллионных долях градуса: 55.751244 -> 55751244. Точность 1e-6° ≈ 0.1 м.
 */
#pragma once

#include <cstdint>
#include <optional>
#include <string_view>

#include "text.hpp"

namespace gps {

struct Time {
    uint16_t year   = 0;
    uint8_t  month  = 0;
    uint8_t  day    = 0;
    uint8_t  hour   = 0;
    uint8_t  minute = 0;
    uint8_t  second = 0;
};

struct Fix {
    bool     gnss_on       = false; // GNSS-приёмник включён (AT+CGNSPWR=1)
    bool     fix           = false; // есть валидные координаты
    int32_t  lat_e6        = 0;     // широта, +север
    int32_t  lon_e6        = 0;     // долгота, +восток
    int32_t  alt_m         = 0;     // высота над уровнем моря
    uint32_t speed_kmh_x10 = 0;     // скорость, 0.1 км/ч
    uint16_t course_deg    = 0;
    uint16_t hdop_x10      = 0;
    uint8_t  sats_used     = 0;
    uint8_t  sats_in_view  = 0;
    Time     utc;
};

std::optional<Fix> parse_cgnsinf(std::string_view line);
void append_deg(TextBuffer &out, int32_t value_e6);

} // namespace gps
