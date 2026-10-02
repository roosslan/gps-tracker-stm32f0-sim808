#include "sim808.hpp"

#include "at.hpp"
#include "board.hpp"
#include "uart.hpp"

namespace sim808 {
namespace {

constexpr uint32_t kCmdTimeoutMs = 2000;
constexpr uint32_t kSmsSendTimeoutMs = 60000;
constexpr char kCtrlZ = 0x1A;
constexpr char kEsc = 0x1B;

/*
 * Проверяет, отвечает ли модуль: шлёт "AT" до attempts раз и ждёт OK
 * по 300 мс на каждую попытку. Первые "AT" после включения модулю нужны
 * ещё и для autobaud: по ним он определяет скорость UART, поэтому первые
 * попытки часто остаются без ответа.
 *
 * Параметры:
 *   attempts — сколько раз попробовать (время ожидания — до attempts * 0.3 с).
 *
 * Возвращает: true, как только пришёл OK; false — модуль молчит.
 */
bool probe(unsigned attempts)
{
    for (unsigned i = 0; i < attempts; i++) {
        if (at::cmd("AT", 300) == at::Result::Ok)
            return true;
    }
    return false;
}

/*
 * «Нажимает» кнопку питания модуля: держит PWRKEY у земли 1.5 с и отпускает.
 * Выключенный модуль от этого включается, включённый — выключается, поэтому
 * вызывать только если модуль не отвечает. После включения модулю нужно
 * ещё ~3 с на загрузку.
 * Параметров нет.
 */
void press_pwrkey()
{
    logger::print("SIM808 silent, pressing PWRKEY");
    board::pwrkey(true);
    board::delay_ms(1500);
    board::pwrkey(false);
}

/*
 * Выполняет команду настройки, повторяя её до 15 раз с паузой в 1 с.
 * Сразу после включения модуль ещё загружается, и SMS-команды отвечают
 * ERROR, пока он не сообщит "SMS Ready" (обычно 5–15 с). Если все попытки
 * не удались, пишет ошибку в лог.
 *
 * Параметры:
 *   command — команда без "\r", например "AT+CMGF=1".
 *
 * Возвращает: true — команда прошла; false — за ~45 с так и не прошла.
 */
bool cmd_retry(std::string_view command)
{
    for (int i = 0; i < 15; i++) {
        if (at::cmd(command, kCmdTimeoutMs) == at::Result::Ok)
            return true;
        board::delay_ms(1000);
    }
    auto err = at::last_error();
    logger::print("%.*s failed: %.*s", static_cast<int>(command.size()), command.data(),
                  static_cast<int>(err.size()), err.data());
    return false;
}

/*
 * Пропускает n разделителей sep и читает целое число сразу за последним.
 * Пример: int_after("+CBC: 0,85,4105", ',', 2) = 4105.
 *
 * Параметры:
 *   s   — строка ответа модуля;
 *   sep — символ-разделитель;
 *   n   — сколько разделителей пропустить (1 — число после первого).
 *
 * Возвращает: число или std::nullopt, если разделителей в строке меньше n
 * или за последним из них не число. Пробелы перед числом пропускаются.
 */
std::optional<int> int_after(std::string_view s, char sep, int n)
{
    for (int i = 0; i < n; i++) {
        size_t p = s.find(sep);
        if (p == std::string_view::npos)
            return std::nullopt;
        s.remove_prefix(p + 1);
    }
    while (!s.empty() && s.front() == ' ')
        s.remove_prefix(1);
    return text::parse_int(s);
}

} // namespace

/*
 * Включает и настраивает модуль. Порядок:
 *   1. проверяет, отвечает ли модуль; если нет — нажимает PWRKEY и ждёт
 *      до ~12 с, пока он загрузится;
 *   2. пишет в лог версию прошивки модуля (ATI);
 *   3. выключает эхо (ATE0), включает текстовые ошибки (AT+CMEE=2),
 *      текстовый режим SMS (AT+CMGF=1) с кодировкой GSM (AT+CSCS="GSM")
 *      и уведомления о новых SMS через +CMTI (AT+CNMI=2,1,0,0,0);
 *   4. проверяет SIM-карту (AT+CPIN?) — если стоит PIN, только пишет
 *      предупреждение в лог;
 *   5. включает GNSS-приёмник (AT+CGNSPWR=1).
 * Регистрацию в сети не ждёт: она приходит позже сама.
 * Занимает от ~1 с (модуль уже включён) до минуты. Параметров нет.
 *
 * Возвращает: true — модуль отвечает и настроен; false — не отвечает
 * или не принял команды настройки.
 */
bool start()
{
    if (!probe(5)) {
        press_pwrkey();
        board::delay_ms(3000);
        if (!probe(30)) {
            logger::print("SIM808 does not answer. Check wiring, power and PWRKEY");
            return false;
        }
    }
    logger::print("SIM808 answers");

    Text<64> line;
    if (at::query("ATI", "SIM", line, kCmdTimeoutMs))
        logger::print("module: %s", line.c_str());

    bool ok = cmd_retry("ATE0")
           && cmd_retry("AT+CMEE=2")
           && cmd_retry("AT+CMGF=1")
           && cmd_retry("AT+CSCS=\"GSM\"")
           && cmd_retry("AT+CNMI=2,1,0,0,0");
    if (!ok)
        return false;

    if (!at::query("AT+CPIN?", "+CPIN:", line, 5000) || line.view() != "+CPIN: READY") {
        std::string_view why = line.empty() ? at::last_error() : line.view();
        logger::print("SIM not ready (%.*s). Remove the PIN code from the SIM card",
                      static_cast<int>(why.size()), why.data());
    }

    gnss_power(true);
    logger::print("SIM808 ready: SMS and GNSS configured");
    return true;
}

/*
 * Быстрая проверка связи с модулем: до 3 попыток "AT", всего до ~1 с.
 * Параметров нет.
 *
 * Возвращает: true, если модуль ответил OK.
 */
bool alive()
{
    return probe(3);
}

/*
 * Включает или выключает GNSS-приёмник модуля (AT+CGNSPWR). После включения
 * до первого фикса проходит от ~30 с (под открытым небом) до нескольких минут.
 *
 * Параметры:
 *   on — true: включить, false: выключить.
 *
 * Возвращает: true, если модуль ответил OK.
 */
bool gnss_power(bool on)
{
    return at::cmd(on ? "AT+CGNSPWR=1" : "AT+CGNSPWR=0", kCmdTimeoutMs) == at::Result::Ok;
}

/*
 * Запрашивает у модуля текущее состояние GNSS (AT+CGNSINF) и разбирает ответ.
 * Параметров нет.
 *
 * Возвращает: состояние GNSS (fix = false, если фикса сейчас нет) или
 * std::nullopt, если модуль не ответил, ответил ERROR или прислал
 * непонятную строку.
 */
std::optional<gps::Fix> gnss_read()
{
    Text<160> line;
    if (!at::query("AT+CGNSINF", "+CGNSINF:", line, kCmdTimeoutMs))
        return std::nullopt;
    return gps::parse_cgnsinf(line.view());
}

/*
 * Просит у модуля список непрочитанных SMS (AT+CMGL="REC UNREAD",1).
 * Сами номера ячеек подбирает AT-движок из строк +CMGL и ставит в очередь,
 * откуда их берёт at::pop_sms(). Нужна при старте (SMS, пришедшие, пока
 * трекер был выключен) и периодически — на случай, если уведомление +CMTI
 * потерялось. Параметр ",1" не даёт модулю пометить SMS прочитанными.
 * Параметров нет.
 */
void scan_unread()
{
    at::cmd("AT+CMGL=\"REC UNREAD\",1", 10000);
}

/*
 * Читает SMS из ячейки памяти SIM (AT+CMGR). SMS после этого помечается
 * прочитанной, но не удаляется: для этого — delete_sms().
 *
 * Параметры:
 *   index — номер ячейки (из +CMTI/+CMGL);
 *   out   — куда записать номер отправителя и текст (только первая строка,
 *           если текст многострочный; пустой, если текста нет; длинный
 *           текст обрезается до 160 символов).
 *
 * Возвращает: true — SMS прочитана; false — ячейка пуста, модуль ответил
 * ошибкой или заголовок не разобрался.
 */
bool read_sms(int index, Sms &out)
{
    Text<20> command;
    command.appendf("AT+CMGR=%d", index);
    Text<sms::kTextMax + 120> resp;
    if (at::cmd(command.view(), 5000, &resp) != at::Result::Ok)
        return false;

    std::string_view all = resp.view();
    size_t nl = all.find('\n');
    std::string_view header = text::head(all, nl);
    auto sender = sms::parse_cmgr_sender(header);
    if (!sender)
        return false;

    out.number.clear();
    out.number.append(*sender);
    out.text.clear();
    if (nl != std::string_view::npos) {
        std::string_view body = all;
        body.remove_prefix(nl + 1);
        out.text.append(text::head(body, body.find('\n')));
    }
    return true;
}

/*
 * Удаляет SMS из ячейки памяти SIM (AT+CMGD). Трекер удаляет каждую
 * прочитанную SMS, чтобы память SIM (обычно 20–50 сообщений)
 * не переполнилась и новые SMS продолжали приходить.
 *
 * Параметры:
 *   index — номер ячейки.
 *
 * Возвращает: true, если модуль ответил OK.
 */
bool delete_sms(int index)
{
    Text<20> command;
    command.appendf("AT+CMGD=%d", index);
    return at::cmd(command.view(), 5000) == at::Result::Ok;
}

/*
 * Отправляет SMS в текстовом режиме: AT+CMGS="номер", ждёт приглашение "> ",
 * пишет текст и завершает его Ctrl+Z. Если приглашение не пришло, отправляет
 * ESC, чтобы модуль не остался в режиме ввода текста. Ожидание подтверждения
 * от сети — до 60 с, сторожевой таймер в это время кормится.
 *
 * Параметры:
 *   number — номер получателя, например "+79161234567" (кавычек быть не должно);
 *   text   — текст SMS: не длиннее 160 символов, только символы алфавита GSM
 *            (латиница, цифры, обычная пунктуация, '\n'); символы 0x1A и 0x1B
 *            недопустимы — модуль воспримет их как «отправить» и «отменить».
 *
 * Возвращает: true — сеть приняла SMS; false — нет приглашения, ошибка сети
 * (например, нет денег или регистрации) или таймаут. Причина — в логе.
 */
bool send_sms(std::string_view number, std::string_view text)
{
    Text<sms::kNumberMax + 16> command;
    command.append("AT+CMGS=\"");
    command.append(number);
    command.append('"');

    if (!at::cmd_prompt(command.view(), 5000)) {
        at::write(kEsc);
        at::wait_result(2000);
        auto err = at::last_error();
        logger::print("no SMS prompt: %.*s", static_cast<int>(err.size()), err.data());
        return false;
    }
    at::write(text);
    at::write(kCtrlZ);
    if (at::wait_result(kSmsSendTimeoutMs) != at::Result::Ok) {
        auto err = at::last_error();
        logger::print("SMS send failed: %.*s", static_cast<int>(err.size()), err.data());
        return false;
    }
    return true;
}

/*
 * Уровень сигнала GSM (AT+CSQ). 0 — очень слабый, 31 — отличный; меньше 10 —
 * SMS могут не уходить. 1 шаг ≈ 2 дБ: значение = (дБм + 113) / 2.
 * Параметров нет.
 *
 * Возвращает: 0..31 или 99, если уровень неизвестен (нет сети
 * или модуль не ответил).
 */
int signal()
{
    Text<32> line;
    if (!at::query("AT+CSQ", "+CSQ:", line, kCmdTimeoutMs))
        return 99;
    return int_after(line.view(), ':', 1).value_or(99);
}

/*
 * Проверяет регистрацию в сети GSM (AT+CREG?). Без неё SMS не принимаются
 * и не отправляются. Параметров нет.
 *
 * Возвращает: true — зарегистрирован в домашней сети (стат. 1) или
 * в роуминге (стат. 5); false — ищет сеть, отказано или модуль не ответил.
 */
bool registered()
{
    Text<32> line;
    if (!at::query("AT+CREG?", "+CREG:", line, kCmdTimeoutMs))
        return false;
    auto stat = int_after(line.view(), ',', 1);
    return stat == 1 || stat == 5;
}

/*
 * Читает заряд и напряжение питания модуля (AT+CBC: "+CBC: 0,85,4105").
 * Процент осмыслен только при питании от Li-ion аккумулятора на VBAT;
 * от блока питания через регулятор это просто напряжение на выходе
 * регулятора. Параметров нет.
 *
 * Возвращает: заряд и напряжение или std::nullopt, если модуль не ответил
 * или ответ не разобрался.
 */
std::optional<Battery> battery()
{
    Text<32> line;
    if (!at::query("AT+CBC", "+CBC:", line, kCmdTimeoutMs))
        return std::nullopt;
    auto pct = int_after(line.view(), ',', 1);
    auto mv = int_after(line.view(), ',', 2);
    if (!pct || !mv || *pct < 0 || *mv <= 0)
        return std::nullopt;
    return Battery{*pct, *mv};
}

} // namespace sim808
