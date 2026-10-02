#include "gps.hpp"

#include <array>

namespace gps {
namespace {

enum Field : size_t {
    kRun = 0, kFixStatus, kUtc, kLat, kLon, kAlt, kSpeed, kCourse,
    kHdop = 10, kSatsInView = 14, kSatsUsed = 15,
    kMinFieldsForFix = kCourse + 1,
};

constexpr size_t kMaxFields = 22;

struct Fields {
    std::array<std::string_view, kMaxFields> items;
    size_t count = 0;

    /*
     * Поле по номеру (см. enum Field).
     *
     * Параметры:
     *   i — номер поля; вызывающий проверяет, что i < count.
     *
     * Возвращает: содержимое поля (пустое, если модуль его не заполнил).
     */
    std::string_view operator[](size_t i) const { return items[i]; }
};

/*
 * Режет строку с полями CGNSINF по запятым, ничего не копируя: каждое поле —
 * string_view внутрь исходной строки. Пустое поле (две запятые подряд — SIM808
 * так присылает незаполненные значения) даёт пустой string_view. Строка
 * заканчивается на первом '\r' или '\n', если они есть.
 *
 * Параметры:
 *   s — строка сразу после "+CGNSINF: ".
 *
 * Возвращает: найденные поля (минимум одно, даже для пустой строки).
 * Если полей больше kMaxFields, лишние отбрасываются.
 */
Fields split_fields(std::string_view s)
{
    if (size_t end = s.find_first_of("\r\n"); end != std::string_view::npos)
        s = text::head(s, end);

    Fields f;
    for (;;) {
        size_t comma = s.find(',');
        f.items[f.count++] = text::head(s, comma);
        if (comma == std::string_view::npos || f.count == kMaxFields)
            return f;
        s.remove_prefix(comma + 1);
    }
}

/*
 * Переводит десятичное число из текста в целое с фиксированной точкой,
 * без float (FPU у Cortex-M0 нет). Пример: "-12.3456" при decimals = 6
 * даёт -12345600. Знаки после точки сверх decimals отбрасываются без
 * округления, недостающие дополняются нулями. Допускается знак '+' или '-'
 * в начале и одна точка.
 *
 * Параметры:
 *   s        — поле с числом;
 *   decimals — сколько знаков после точки оставить, то есть на 10^decimals
 *              умножается значение.
 *
 * Возвращает: число или std::nullopt, если поле пустое, в нём нет цифр,
 * есть посторонние символы или вторая точка, или результат не влезает в int32.
 */
std::optional<int32_t> parse_fixed(std::string_view s, int decimals)
{
    bool neg = false;
    int64_t v = 0;
    int frac = -1; // -1: точка ещё не встречалась
    bool digits = false;

    if (!s.empty() && (s.front() == '-' || s.front() == '+')) {
        neg = s.front() == '-';
        s.remove_prefix(1);
    }
    for (char c : s) {
        if (c == '.' && frac < 0) {
            frac = 0;
        } else if (c >= '0' && c <= '9') {
            digits = true;
            if (frac < 0) {
                v = v * 10 + (c - '0');
            } else if (frac < decimals) {
                v = v * 10 + (c - '0');
                frac++;
            }
            if (v > INT32_MAX)
                return std::nullopt;
        } else {
            return std::nullopt;
        }
    }
    if (!digits)
        return std::nullopt;
    for (int i = frac < 0 ? 0 : frac; i < decimals; i++)
        v *= 10;
    if (v > INT32_MAX)
        return std::nullopt;
    return static_cast<int32_t>(neg ? -v : v);
}

/*
 * Читает ровно n десятичных цифр начиная с позиции pos. Используется
 * для разбора даты и времени фиксированной ширины.
 *
 * Параметры:
 *   s   — строка с цифрами;
 *   pos — с какого символа читать;
 *   n   — сколько цифр прочитать (для int не больше 9).
 *
 * Возвращает: значение числа или -1, если строка короче pos + n
 * или среди этих символов есть не цифра.
 */
int digits_at(std::string_view s, size_t pos, size_t n)
{
    if (pos + n > s.size())
        return -1;
    int v = 0;
    for (size_t i = pos; i < pos + n; i++) {
        if (s[i] < '0' || s[i] > '9')
            return -1;
        v = v * 10 + (s[i] - '0');
    }
    return v;
}

/*
 * Разбирает дату и время UTC из CGNSINF в формате yyyyMMddhhmmss.sss.
 * Миллисекунды игнорируются. Каждое значение проверяется на допустимый
 * диапазон (месяц 1..12, час 0..23 и т.д.; секунда 60 допускается ради
 * високосной секунды).
 *
 * Параметры:
 *   s — поле с датой и временем.
 *
 * Возвращает: время или std::nullopt, если поле короче 14 символов,
 * в нём не цифры или значения вне диапазона.
 */
std::optional<Time> parse_utc(std::string_view s)
{
    int y = digits_at(s, 0, 4), mo = digits_at(s, 4, 2), d = digits_at(s, 6, 2);
    int h = digits_at(s, 8, 2), mi = digits_at(s, 10, 2), sec = digits_at(s, 12, 2);
    if (y < 0 || mo < 1 || mo > 12 || d < 1 || d > 31 || h < 0 || h > 23 ||
        mi < 0 || mi > 59 || sec < 0 || sec > 60)
        return std::nullopt;
    Time t;
    t.year = static_cast<uint16_t>(y);
    t.month = static_cast<uint8_t>(mo);
    t.day = static_cast<uint8_t>(d);
    t.hour = static_cast<uint8_t>(h);
    t.minute = static_cast<uint8_t>(mi);
    t.second = static_cast<uint8_t>(sec);
    return t;
}

/*
 * Разбирает неотрицательное число с фиксированной точкой для необязательных
 * полей CGNSINF (скорость, курс, HDOP, число спутников), которые модуль
 * часто оставляет пустыми.
 *
 * Параметры:
 *   s        — поле с числом;
 *   decimals — сколько знаков после точки сохранить (см. parse_fixed).
 *
 * Возвращает: значение или 0, если поле пустое, повреждено или отрицательное.
 */
uint32_t parse_uint_or0(std::string_view s, int decimals)
{
    auto v = parse_fixed(s, decimals);
    return v && *v > 0 ? static_cast<uint32_t>(*v) : 0;
}

} // namespace

/*
 * Разбирает ответ модуля на AT+CGNSINF. Строка ищется по префиксу
 * "+CGNSINF:", так что перед ним может быть мусор. Если модуль сообщает,
 * что фикса нет, или сообщает о фиксе, но прислал координаты вне
 * допустимого диапазона (|широта| > 90°, |долгота| > 180°) или не число,
 * результат всё равно возвращается, но с fix = false. Высота, скорость,
 * курс, HDOP и спутники разбираются, только если координаты валидны;
 * отсутствующие поля остаются 0.
 *
 * Параметры:
 *   line — строка ответа модуля, например
 *          "+CGNSINF: 1,1,20260918093015.000,48.148598,17.107748,...";
 *          может заканчиваться "\r\n".
 *
 * Возвращает: состояние GNSS или std::nullopt, если это не CGNSINF
 * или первое поле (статус GNSS) повреждено.
 */
std::optional<Fix> parse_cgnsinf(std::string_view line)
{
    constexpr std::string_view prefix = "+CGNSINF:";
    size_t p = line.find(prefix);
    if (p == std::string_view::npos)
        return std::nullopt;
    line.remove_prefix(p + prefix.size());
    while (!line.empty() && line.front() == ' ')
        line.remove_prefix(1);

    Fields f = split_fields(line);
    if (f.count < 2 || f[kRun].size() != 1)
        return std::nullopt;

    Fix out;
    out.gnss_on = f[kRun][0] == '1';
    if (f.count < kMinFieldsForFix || f[kFixStatus] != "1")
        return out;

    auto lat = parse_fixed(f[kLat], 6);
    auto lon = parse_fixed(f[kLon], 6);
    if (!lat || !lon || *lat < -90000000 || *lat > 90000000 || *lon < -180000000 || *lon > 180000000)
        return out; // модуль сказал "fix", но координаты мусорные — считаем, что фикса нет

    out.fix = true;
    out.lat_e6 = *lat;
    out.lon_e6 = *lon;
    if (auto alt = parse_fixed(f[kAlt], 0))
        out.alt_m = *alt;
    out.speed_kmh_x10 = parse_uint_or0(f[kSpeed], 1);
    out.course_deg = static_cast<uint16_t>(parse_uint_or0(f[kCourse], 0));
    if (auto t = parse_utc(f[kUtc]))
        out.utc = *t;
    if (f.count > kHdop)
        out.hdop_x10 = static_cast<uint16_t>(parse_uint_or0(f[kHdop], 1));
    if (f.count > kSatsInView)
        out.sats_in_view = static_cast<uint8_t>(parse_uint_or0(f[kSatsInView], 0));
    if (f.count > kSatsUsed)
        out.sats_used = static_cast<uint8_t>(parse_uint_or0(f[kSatsUsed], 0));
    return out;
}

/*
 * Дописывает координату из миллионных долей градуса в виде "-12.345678"
 * (всегда 6 знаков после точки). Знак выводится отдельно: у значений между
 * -1° и 0° целая часть равна 0, и обычный "%ld" минус бы потерял.
 *
 * Параметры:
 *   out      — строка, в конец которой дописывается координата; самая
 *              длинная, "-180.000000", занимает 11 символов;
 *   value_e6 — координата в миллионных долях градуса (55751244 = 55.751244°).
 */
void append_deg(TextBuffer &out, int32_t value_e6)
{
    uint32_t a = value_e6 < 0 ? static_cast<uint32_t>(-static_cast<int64_t>(value_e6))
                              : static_cast<uint32_t>(value_e6);
    out.appendf("%s%lu.%06lu", value_e6 < 0 ? "-" : "",
                static_cast<unsigned long>(a / 1000000u), static_cast<unsigned long>(a % 1000000u));
}

} // namespace gps
