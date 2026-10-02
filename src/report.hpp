/*
 * Тексты SMS-ответов. Всё укладывается в одну SMS (160 символов GSM 7-bit),
 * поэтому только латиница.
 */
#pragma once

#include <cstdint>
#include <optional>

#include "gps.hpp"
#include "text.hpp"

namespace report {

// Фикс старше этого считается устаревшим, и в ответе указывается его возраст.
inline constexpr uint32_t kFreshS = 30;

struct Status {
    int      csq         = 99;    // AT+CSQ: 0..31, 99 = неизвестно
    bool     registered  = false; // зарегистрирован в сети (дом или роуминг)
    int      battery_pct = -1;    // AT+CBC, -1 = неизвестно
    int      battery_mv  = 0;
    bool     gnss_on     = false;
    bool     have_fix    = false; // был ли хоть один фикс с момента включения
    uint32_t fix_age_s   = 0;
    uint8_t  sats_used   = 0;
    uint32_t uptime_s    = 0;
};

void where(TextBuffer &out, const std::optional<gps::Fix> &last, uint32_t age_s, bool gnss_on);
void status(TextBuffer &out, const Status &st);
void help(TextBuffer &out);

} // namespace report
