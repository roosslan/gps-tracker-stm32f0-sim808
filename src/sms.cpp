#include "sms.hpp"

namespace sms {
namespace {

constexpr size_t kMatchDigits = 10;
constexpr size_t kMinDigits = 5;

/*
 * Общий разбор строк вида "<prefix> ...<sep><число>": проверяет, что строка
 * начинается с prefix, при необходимости пропускает всё до первого
 * разделителя sep и читает стоящее за ним целое число (пробелы перед
 * числом допускаются).
 *
 * Параметры:
 *   line   — строка от модуля;
 *   prefix — обязательное начало строки, например "+CMTI:";
 *   sep    — разделитель, после которого стоит число, или пустая строка,
 *            если число идёт сразу после префикса.
 *
 * Возвращает: число или std::nullopt, если префикс другой, разделителя
 * нет или после него не цифра.
 */
std::optional<int> parse_index_after(std::string_view line, std::string_view prefix, std::string_view sep)
{
    if (!line.starts_with(prefix))
        return std::nullopt;
    line.remove_prefix(prefix.size());
    if (!sep.empty()) {
        size_t p = line.find(sep);
        if (p == std::string_view::npos)
            return std::nullopt;
        line.remove_prefix(p + sep.size());
    }
    while (!line.empty() && line.front() == ' ')
        line.remove_prefix(1);
    if (line.empty() || line.front() < '0' || line.front() > '9')
        return std::nullopt;
    return text::parse_int(line);
}

/*
 * Сравнивает две строки без учёта регистра латинских букв.
 *
 * Параметры:
 *   a — первая строка;
 *   b — вторая строка (в коде — команда в нижнем регистре).
 *
 * Возвращает: true, если строки совпадают с точностью до регистра.
 */
bool iequals(std::string_view a, std::string_view b)
{
    if (a.size() != b.size())
        return false;
    for (size_t i = 0; i < a.size(); i++) {
        char x = a[i], y = b[i];
        if (x >= 'A' && x <= 'Z')
            x = static_cast<char>(x - 'A' + 'a');
        if (y >= 'A' && y <= 'Z')
            y = static_cast<char>(y - 'A' + 'a');
        if (x != y)
            return false;
    }
    return true;
}

/*
 * Оставляет от телефонного номера только цифры (ведущий '+' отбрасывается)
 * и заодно проверяет, что это вообще телефонный номер.
 *
 * Параметры:
 *   s   — номер как его прислал модуль, например "+79161234567";
 *   out — куда записать цифры; прежнее содержимое стирается.
 *
 * Возвращает: false — в номере есть не цифры (буквенный отправитель вроде
 * "MTS"), номер не влезает в out или в нём меньше 5 цифр (короткие
 * сервисные номера вроде "900"); true — номер годится.
 */
bool number_digits(std::string_view s, Number &out)
{
    out.clear();
    if (!s.empty() && s.front() == '+')
        s.remove_prefix(1);
    if (s.size() > out.capacity())
        return false;
    for (char c : s) {
        if (c < '0' || c > '9')
            return false;
        out.append(c);
    }
    return out.size() >= kMinDigits;
}

/*
 * Возвращает последние kMatchDigits (10) цифр номера. По ним номера
 * сравниваются, чтобы "+79161234567" и "89161234567" совпадали независимо
 * от кода страны и префикса.
 *
 * Параметры:
 *   digits — номер из одних цифр (результат number_digits).
 *
 * Возвращает: последние 10 цифр или весь номер, если цифр меньше 10.
 */
std::string_view tail(std::string_view digits)
{
    if (digits.size() > kMatchDigits)
        digits.remove_prefix(digits.size() - kMatchDigits);
    return digits;
}

} // namespace

/*
 * Разбирает уведомление модуля о новой SMS: "+CMTI: "SM",3". Модуль шлёт его
 * сам, когда SMS сохранена в памяти (режим AT+CNMI=2,1).
 *
 * Параметры:
 *   line — строка от модуля.
 *
 * Возвращает: номер ячейки памяти, где лежит SMS (3 в примере),
 * или std::nullopt, если это не +CMTI.
 */
std::optional<int> parse_cmti(std::string_view line)
{
    return parse_index_after(line, "+CMTI:", ",");
}

/*
 * Разбирает заголовок SMS в ответе на AT+CMGL (список сообщений):
 * "+CMGL: 3,"REC UNREAD","+79161234567",...". Нужен только номер ячейки,
 * остальное прочитается потом через AT+CMGR.
 *
 * Параметры:
 *   line — строка от модуля.
 *
 * Возвращает: номер ячейки (3 в примере) или std::nullopt, если это не +CMGL.
 */
std::optional<int> parse_cmgl(std::string_view line)
{
    return parse_index_after(line, "+CMGL:", "");
}

/*
 * Достаёт номер отправителя из заголовка ответа на AT+CMGR:
 * "+CMGR: "REC UNREAD","+79161234567","","26/09/28,12:00:00+12".
 * Номер — это вторая строка в кавычках. Буквенные отправители ("MTS")
 * тоже возвращаются: отсекать их — задача sender_allowed().
 *
 * Параметры:
 *   line — строка заголовка от модуля.
 *
 * Возвращает: номер (string_view внутрь line, действителен, пока жива line)
 * или std::nullopt, если это не +CMGR, номер пустой, кавычки не на месте
 * или номер длиннее kNumberMax - 1 символов.
 */
std::optional<std::string_view> parse_cmgr_sender(std::string_view line)
{
    if (!line.starts_with("+CMGR:"))
        return std::nullopt;
    for (int i = 0; i < 3; i++) {
        size_t q = line.find('"');
        if (q == std::string_view::npos)
            return std::nullopt;
        line.remove_prefix(q + 1);
    }
    size_t end = line.find('"');
    if (end == std::string_view::npos || end == 0 || end >= kNumberMax)
        return std::nullopt;
    return text::head(line, end);
}

/*
 * Распознаёт команду в тексте SMS. Пробелы и переводы строк по краям
 * отбрасываются, регистр не важен. Команда должна быть единственным словом
 * в сообщении, поэтому "where are you" и рекламные рассылки командами
 * не считаются и трекер на них не отвечает.
 *
 * Параметры:
 *   text — текст SMS.
 *
 * Возвращает:
 *   Command::Where  — "where", "gde", "loc" или "?";
 *   Command::Status — "status";
 *   Command::Help   — "help";
 *   Command::None   — всё остальное, включая пустой текст и несколько слов.
 */
Command parse_command(std::string_view text)
{
    std::string_view word = text::trim(text);
    for (char c : word)
        if (text::is_space(c))
            return Command::None;

    if (iequals(word, "where") || iequals(word, "gde") || iequals(word, "loc") || word == "?")
        return Command::Where;
    if (iequals(word, "status"))
        return Command::Status;
    if (iequals(word, "help"))
        return Command::Help;
    return Command::None;
}

/*
 * Решает, можно ли отвечать отправителю SMS. Буквенные и короткие номера
 * не проходят никогда. Остальные сравниваются с белым списком по последним
 * 10 цифрам.
 *
 * Параметры:
 *   number  — номер отправителя из заголовка SMS;
 *   allowed — белый список номеров. Элементы nullptr пропускаются (в конфиге
 *             список заканчивается NULL). Если в списке нет ни одного
 *             номера, разрешён любой телефонный номер.
 *
 * Возвращает: true — отвечать можно.
 */
bool sender_allowed(std::string_view number, std::span<const char *const> allowed)
{
    Number digits;
    if (!number_digits(number, digits))
        return false;

    bool any = false;
    for (const char *entry : allowed) {
        if (!entry)
            continue;
        any = true;
        Number entry_digits;
        if (number_digits(entry, entry_digits) && tail(digits.view()) == tail(entry_digits.view()))
            return true;
    }
    return !any;
}

} // namespace sms
