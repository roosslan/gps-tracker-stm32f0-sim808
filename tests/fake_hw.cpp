#include "fake_hw.hpp"

#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <deque>
#include <map>

#include "board.hpp"
#include "uart.hpp"

namespace {

struct State {
    uint32_t now = 0;
    std::map<std::string, std::string> replies;
    std::deque<char> rx;        // что «модуль» ещё не отдал в UART
    std::string tx;             // всё, что прошивка отправила модулю
    std::string line;           // текущая собираемая команда
    bool sms_text_mode = false; // после AT+CMGS и "> " — ждём текст до Ctrl+Z
    std::vector<std::string> log;
    int pwrkey_presses = 0;
};

State st;

/*
 * Кладёт байты в приёмный буфер, как будто их прислал модуль.
 *
 * Параметры:
 *   bytes — строка, которую получит прошивка через sim_uart::get().
 */
void push_rx(const std::string &bytes)
{
    for (char c : bytes)
        st.rx.push_back(c);
}

} // namespace

namespace fake {

/*
 * Возвращает поддельное железо в исходное состояние: время 0, ответов
 * модуля нет, буферы и лог пусты. Вызывать в начале каждого теста.
 * Параметров нет.
 */
void reset()
{
    st = State{};
}

/*
 * Задаёт ответ модуля на команду. Когда прошивка отправит command и '\r',
 * в приёмный буфер ляжет reply. Если в ответе есть "> " (приглашение
 * AT+CMGS), модуль переходит в режим приёма текста SMS: дальше байты
 * копятся до Ctrl+Z, после которого приходит ответ, заданный для "\x1A".
 *
 * Параметры:
 *   command — команда без '\r', например "AT+CSQ"; "\x1A" — ответ на Ctrl+Z;
 *   reply   — что ответит модуль, с "\r\n" как у настоящего.
 */
void respond(const std::string &command, const std::string &reply)
{
    st.replies[command] = reply;
}

/*
 * Кладёт в приёмный буфер незапрошенное сообщение модуля (URC), например
 * "+CMTI: \"SM\",3\r\n". Прошивка увидит его при следующем at::poll().
 *
 * Параметры:
 *   bytes — текст сообщения с "\r\n".
 */
void unsolicited(const std::string &bytes)
{
    push_rx(bytes);
}

/*
 * Всё, что прошивка отправила модулю с начала теста.
 * Параметров нет.
 *
 * Возвращает: отправленные байты, включая '\r', Ctrl+Z и ESC.
 */
const std::string &sent()
{
    return st.tx;
}

/*
 * Проверяет, писала ли прошивка в лог строку, содержащую text.
 *
 * Параметры:
 *   text — искомый фрагмент.
 *
 * Возвращает: true, если хотя бы одна строка лога его содержит.
 */
bool log_contains(std::string_view text)
{
    for (const auto &l : st.log)
        if (l.find(text) != std::string::npos)
            return true;
    return false;
}

/*
 * Сколько раз прошивка «нажимала» PWRKEY с начала теста.
 * Параметров нет.
 *
 * Возвращает: число нажатий.
 */
int pwrkey_presses()
{
    return st.pwrkey_presses;
}

} // namespace fake

namespace board {

/*
 * Поддельное время: каждый вызов сдвигает его на 1 мс, так что циклы
 * ожидания с таймаутом в тестах всегда заканчиваются.
 * Параметров нет.
 *
 * Возвращает: «текущее» время в миллисекундах.
 */
uint32_t millis()
{
    return ++st.now;
}

/*
 * Поддельная пауза: мгновенно сдвигает время.
 *
 * Параметры:
 *   ms — на сколько миллисекунд сдвинуть время.
 */
void delay_ms(uint32_t ms)
{
    st.now += ms;
}

/*
 * Сторожевого таймера в тестах нет. Параметров нет.
 */
void watchdog_kick()
{
}

/*
 * Считает нажатия PWRKEY (вызовы с pressed = true).
 *
 * Параметры:
 *   pressed — true: «нажать», false: «отпустить».
 */
void pwrkey(bool pressed)
{
    if (pressed)
        st.pwrkey_presses++;
}

} // namespace board

namespace sim_uart {

/*
 * «Отправляет» байт модулю: запоминает его и, если это конец команды
 * ('\r') или конец текста SMS (Ctrl+Z), кладёт в приёмный буфер
 * заданный ответ.
 *
 * Параметры:
 *   c — отправляемый байт.
 */
void put(char c)
{
    st.tx += c;
    if (st.sms_text_mode) {
        if (c == '\x1A' || c == '\x1B') {
            st.sms_text_mode = false;
            if (c == '\x1A' && st.replies.count("\x1A"))
                push_rx(st.replies["\x1A"]);
        }
        return;
    }
    if (c != '\r') {
        st.line += c;
        return;
    }
    auto it = st.replies.find(st.line);
    if (it != st.replies.end()) {
        push_rx(it->second);
        if (it->second.find("> ") != std::string::npos)
            st.sms_text_mode = true;
    }
    st.line.clear();
}

/*
 * «Отправляет» строку модулю побайтно через put().
 *
 * Параметры:
 *   s — отправляемые байты.
 */
void write(std::string_view s)
{
    for (char c : s)
        put(c);
}

/*
 * Отдаёт очередной байт «от модуля».
 * Параметров нет.
 *
 * Возвращает: байт или std::nullopt, если модуль больше ничего не прислал.
 */
std::optional<char> get()
{
    if (st.rx.empty())
        return std::nullopt;
    char c = st.rx.front();
    st.rx.pop_front();
    return c;
}

} // namespace sim_uart

namespace logger {

/*
 * Запоминает строку лога, чтобы тест мог проверить её через
 * fake::log_contains().
 *
 * Параметры:
 *   fmt — формат printf;
 *   ... — аргументы формата.
 */
void print(const char *fmt, ...)
{
    char buf[256];
    va_list ap;
    va_start(ap, fmt);
    std::vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    st.log.emplace_back(buf);
}

} // namespace logger
