#include "report.hpp"

namespace report {
namespace {

/*
 * Дописывает возраст в коротком человекочитаемом виде с округлением вниз:
 * до 2 минут — в секундах ("95s"), до 2 часов — в минутах ("17min"),
 * до 2 суток — в часах ("5h"), дальше — в сутках ("3d").
 *
 * Параметры:
 *   out — строка, куда дописывать;
 *   s   — возраст в секундах.
 */
void append_age(TextBuffer &out, uint32_t s)
{
    if (s < 120)
        out.appendf("%lus", static_cast<unsigned long>(s));
    else if (s < 2 * 3600)
        out.appendf("%lumin", static_cast<unsigned long>(s / 60));
    else if (s < 2 * 86400)
        out.appendf("%luh", static_cast<unsigned long>(s / 3600));
    else
        out.appendf("%lud", static_cast<unsigned long>(s / 86400));
}

} // namespace

/*
 * Составляет ответ на команду WHERE (не длиннее одной SMS, только латиница).
 * Если фикс свежий (моложе kFreshS), в ответе ссылка на Google Maps,
 * а во второй строке время UTC, скорость, высота и число спутников.
 * Если фикс старше, перед этим добавляется строка "No fix now, last one
 * <возраст> ago". Если фикса не было ни разу, вместо координат приходит
 * подсказка: либо ждать спутников, либо GPS выключен и включается.
 *
 * Параметры:
 *   out     — куда писать текст; прежнее содержимое стирается. Для полной
 *             SMS нужна ёмкость 160 символов (sms::Body), в меньшую строку
 *             текст обрезается;
 *   last    — последний известный фикс или std::nullopt, если фикса ещё не было;
 *   age_s   — сколько секунд прошло с момента фикса last (без фикса
 *             не используется);
 *   gnss_on — включён ли GNSS-приёмник сейчас; влияет только на текст
 *             без фикса.
 */
void where(TextBuffer &out, const std::optional<gps::Fix> &last, uint32_t age_s, bool gnss_on)
{
    out.clear();
    if (!last) {
        out.append(gnss_on
            ? "No GPS fix yet. Antenna needs open sky; first fix can take a few minutes."
            : "GPS is off, trying to turn it on. Retry in a minute.");
        return;
    }

    if (age_s >= kFreshS) {
        out.append("No fix now, last one ");
        append_age(out, age_s);
        out.append(" ago\n");
    }
    out.append("https://maps.google.com/?q=");
    gps::append_deg(out, last->lat_e6);
    out.append(',');
    gps::append_deg(out, last->lon_e6);
    out.append('\n');
    out.appendf("%02u:%02u:%02u UTC spd %lu.%lukm/h alt %ldm sats %u",
                last->utc.hour, last->utc.minute, last->utc.second,
                static_cast<unsigned long>(last->speed_kmh_x10 / 10),
                static_cast<unsigned long>(last->speed_kmh_x10 % 10),
                static_cast<long>(last->alt_m), last->sats_used);
}

/*
 * Составляет ответ на команду STATUS в несколько строк:
 *   "GSM 18/31 reg"          — уровень сигнала и регистрация в сети;
 *   "Power 85% 4.10V"        — заряд и напряжение питания модуля (если известны);
 *   "GPS on, fix, sats 7"    — состояние приёмника и фикса;
 *   "Uptime 2h"              — сколько трекер работает.
 *
 * Параметры:
 *   out — куда писать текст; прежнее содержимое стирается, длинный текст
 *         обрезается;
 *   st  — собранные показатели (см. Status); csq вне 0..31 печатается
 *         как "?", при battery_pct < 0 строка питания пропускается.
 */
void status(TextBuffer &out, const Status &st)
{
    out.clear();
    if (st.csq >= 0 && st.csq <= 31)
        out.appendf("GSM %d/31", st.csq);
    else
        out.append("GSM ?");
    out.append(st.registered ? " reg" : " NOT reg");
    if (st.battery_pct >= 0)
        out.appendf("\nPower %d%% %d.%02dV", st.battery_pct, st.battery_mv / 1000, (st.battery_mv % 1000) / 10);
    out.appendf("\nGPS %s", st.gnss_on ? "on" : "OFF");
    if (!st.have_fix) {
        out.append(", no fix yet");
    } else if (st.fix_age_s < kFreshS) {
        out.appendf(", fix, sats %u", st.sats_used);
    } else {
        out.append(", last fix ");
        append_age(out, st.fix_age_s);
        out.append(" ago");
    }
    out.append("\nUptime ");
    append_age(out, st.uptime_s);
}

/*
 * Пишет ответ на команду HELP: список команд одной строкой.
 *
 * Параметры:
 *   out — куда писать текст; прежнее содержимое стирается, длинный текст
 *         обрезается.
 */
void help(TextBuffer &out)
{
    out.clear();
    out.append("Commands: WHERE - location, STATUS - signal/power/GPS, HELP");
}

} // namespace report
