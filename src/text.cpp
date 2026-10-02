#include "text.hpp"

#include <cstdio>
#include <cstring>

/*
 * Очищает строку: длина становится 0, в начале буфера '\0'.
 * Параметров нет.
 */
void TextBuffer::clear()
{
    len_ = 0;
    data_[0] = '\0';
}

/*
 * Дописывает строку в конец. То, что не помещается, отбрасывается,
 * результат всегда заканчивается '\0'.
 *
 * Параметры:
 *   s — добавляемый текст; может содержать любые байты, кроме '\0'
 *       (после него c_str() покажет текст обрезанным).
 */
void TextBuffer::append(std::string_view s)
{
    size_t n = s.size();
    if (n > capacity() - len_)
        n = capacity() - len_;
    std::memcpy(data_ + len_, s.data(), n);
    len_ += n;
    data_[len_] = '\0';
}

/*
 * Дописывает один символ в конец, если осталось место.
 *
 * Параметры:
 *   c — добавляемый символ.
 */
void TextBuffer::append(char c)
{
    append(std::string_view(&c, 1));
}

/*
 * Дописывает форматированный текст в конец, как snprintf, но продолжая
 * с того места, где закончилась предыдущая запись. Если текст не влезает,
 * он обрезается, и все следующие добавления уже ничего не дописывают.
 *
 * Параметры:
 *   fmt — формат printf (на STM32 без float: newlib-nano собран без него);
 *   ... — аргументы формата.
 */
void TextBuffer::appendf(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vappendf(fmt, ap);
    va_end(ap);
}

/*
 * То же, что appendf, но аргументы передаются готовым va_list —
 * для функций-обёрток вроде logger::print().
 *
 * Параметры:
 *   fmt — формат printf;
 *   ap  — аргументы формата; после вызова использовать его повторно нельзя.
 */
void TextBuffer::vappendf(const char *fmt, va_list ap)
{
    if (len_ >= capacity())
        return;
    int n = std::vsnprintf(data_ + len_, size_ - len_, fmt, ap);
    if (n < 0) {
        data_[len_] = '\0';
        return;
    }
    len_ += static_cast<size_t>(n) < capacity() - len_ ? static_cast<size_t>(n) : capacity() - len_;
}

namespace text {

/*
 * Проверяет, является ли символ пробельным: пробел, табуляция, перевод
 * строки, возврат каретки, вертикальная табуляция или перевод страницы.
 * В отличие от std::isspace не зависит от локали и не ломается на
 * символах со старшим битом.
 *
 * Параметры:
 *   c — проверяемый символ.
 *
 * Возвращает: true для пробельных символов.
 */
bool is_space(char c)
{
    return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\v' || c == '\f';
}

/*
 * Отрезает пробельные символы (см. is_space) в начале и в конце строки.
 *
 * Параметры:
 *   s — исходная строка.
 *
 * Возвращает: часть s без пробелов по краям; пустую, если s из одних пробелов.
 */
std::string_view trim(std::string_view s)
{
    while (!s.empty() && is_space(s.front()))
        s.remove_prefix(1);
    while (!s.empty() && is_space(s.back()))
        s.remove_suffix(1);
    return s;
}

/*
 * Первые n символов строки. В отличие от string_view::substr ничего
 * не проверяет и не бросает исключений (в прошивке они выключены).
 *
 * Параметры:
 *   s — исходная строка;
 *   n — сколько символов взять; если больше длины s, берётся вся строка.
 *
 * Возвращает: начало s длиной min(n, s.size()).
 */
std::string_view head(std::string_view s, size_t n)
{
    return {s.data(), n < s.size() ? n : s.size()};
}

/*
 * Читает целое число в начале строки, как atoi, но отличает «нет числа»
 * от нуля. Допускается знак '-' или '+' перед цифрами. Чтение
 * останавливается на первом символе, который не цифра; пробелы в начале
 * не пропускаются.
 *
 * Параметры:
 *   s — строка, начинающаяся с числа, например "85,4105".
 *
 * Возвращает: число или std::nullopt, если в начале нет ни одной цифры
 * или число не влезает в int.
 */
std::optional<int> parse_int(std::string_view s)
{
    bool neg = false;
    if (!s.empty() && (s.front() == '-' || s.front() == '+')) {
        neg = s.front() == '-';
        s.remove_prefix(1);
    }
    long long v = 0;
    size_t digits = 0;
    for (char c : s) {
        if (c < '0' || c > '9')
            break;
        v = v * 10 + (c - '0');
        if (v > 0x7FFFFFFF)
            return std::nullopt;
        digits++;
    }
    if (!digits)
        return std::nullopt;
    return static_cast<int>(neg ? -v : v);
}

} // namespace text
