// Тесты AT-движка (at.cpp) и команд модуля (sim808.cpp) с поддельным модулем
// из fake_hw.cpp. Собираются компилятором ПК.
#include <string>
#include <utility>

#include "at.hpp"
#include "check.hpp"
#include "fake_hw.hpp"
#include "sim808.hpp"

namespace {

const std::string OK = "\r\nOK\r\n";

/*
 * Готовит чистое окружение для теста: сбрасывает поддельное железо
 * и выбирает из AT-движка всё, что осталось от прошлого теста
 * (очередь SMS, флаг выключения модуля).
 * Параметров нет.
 */
void fresh()
{
    fake::reset();
    at::poll();
    while (at::pop_sms()) {
    }
    at::take_power_down();
}

/*
 * Проверяет итоги команды: OK, ERROR с текстом ошибки модуля и таймаут,
 * когда модуль не отвечает. Параметров нет.
 */
void test_cmd_results()
{
    fresh();
    fake::respond("AT", OK);
    CHECK(at::cmd("AT", 1000) == at::Result::Ok);
    CHECK(at::last_error().empty());

    fake::respond("AT+CMGF=1", "\r\n+CME ERROR: SIM not inserted\r\n");
    CHECK(at::cmd("AT+CMGF=1", 1000) == at::Result::Error);
    CHECK_STR(at::last_error(), "+CME ERROR: SIM not inserted");

    CHECK(at::cmd("AT+NOREPLY", 500) == at::Result::Timeout);
    CHECK_STR(at::last_error(), "timeout");
    CHECK(fake::sent().find("AT+NOREPLY\r") != std::string::npos);
}

/*
 * Проверяет сбор ответа: строки между командой и OK склеиваются через '\n',
 * а at::query берёт только строку с нужным префиксом. Параметров нет.
 */
void test_response_capture()
{
    fresh();
    fake::respond("AT+GSN", "\r\n8691700318\r\n4105\r\n" + OK);
    Text<32> resp;
    CHECK(at::cmd("AT+GSN", 1000, &resp) == at::Result::Ok);
    CHECK_STR(resp.view(), "8691700318\n4105");

    fake::respond("AT+CSQ", "\r\nAT+CSQ\r\n+CSQ: 18,0\r\n" + OK);
    Text<32> line;
    CHECK(at::query("AT+CSQ", "+CSQ:", line, 1000));
    CHECK_STR(line.view(), "+CSQ: 18,0");

    fake::respond("AT+CBC", OK);
    CHECK(!at::query("AT+CBC", "+CBC:", line, 1000)); // OK, но нужной строки нет
}

/*
 * Проверяет уведомления модуля: +CMTI посреди ответа на команду уходит
 * в очередь SMS, а не в ответ; повторы не дублируются; переполненная
 * очередь пишет в лог; NORMAL POWER DOWN выставляет флаг ровно один раз.
 * Параметров нет.
 */
void test_unsolicited()
{
    fresh();
    fake::respond("AT+CSQ", "\r\n+CMTI: \"SM\",4\r\n+CSQ: 20,0\r\n" + OK);
    CHECK(sim808::signal() == 20);
    CHECK(at::pop_sms() == 4);
    CHECK(!at::pop_sms());

    fake::unsolicited("\r\n+CMTI: \"SM\",2\r\n");
    fake::respond("AT+CMGL=\"REC UNREAD\",1",
                  "\r\n+CMGL: 1,\"REC UNREAD\",\"+79161234567\",\"\",\"26/09/28,12:00:00+12\"\r\nOK\r\n"
                  "+CMGL: 2,\"REC UNREAD\",\"+79161234567\",\"\",\"26/09/28,12:01:00+12\"\r\nwhere\r\n" + OK);
    sim808::scan_unread();
    CHECK(at::pop_sms() == 2);
    CHECK(at::pop_sms() == 1);
    CHECK(!at::pop_sms());

    for (int i = 1; i <= 9; i++)
        fake::unsolicited("\r\n+CMTI: \"SM\"," + std::to_string(i) + "\r\n");
    at::poll();
    CHECK(fake::log_contains("SMS queue full, index 9"));

    fresh();
    fake::unsolicited("\r\nNORMAL POWER DOWN\r\n");
    at::poll();
    CHECK(at::take_power_down());
    CHECK(!at::take_power_down());
}

/*
 * Проверяет чтение SMS: номер и текст, включая текст "OK", который нельзя
 * принять за итог команды, и пустую ячейку. Параметров нет.
 */
void test_read_sms()
{
    fresh();
    fake::respond("AT+CMGR=3",
                  "\r\n+CMGR: \"REC UNREAD\",\"+79161234567\",\"\",\"26/09/28,12:00:00+12\"\r\nOK\r\n" + OK);
    sim808::Sms msg;
    CHECK(sim808::read_sms(3, msg));
    CHECK_STR(msg.number.view(), "+79161234567");
    CHECK_STR(msg.text.view(), "OK");

    fake::respond("AT+CMGR=5", OK);
    CHECK(!sim808::read_sms(5, msg));
}

/*
 * Проверяет отправку SMS: команда, приглашение "> ", текст и Ctrl+Z;
 * ошибка вместо приглашения; отсутствие ответа на Ctrl+Z.
 * Параметров нет.
 */
void test_send_sms()
{
    fresh();
    fake::respond("AT+CMGS=\"+79161234567\"", "\r\n> ");
    fake::respond("\x1A", "\r\n+CMGS: 7\r\n" + OK);
    CHECK(sim808::send_sms("+79161234567", "hello\nworld"));
    CHECK(fake::sent().find("AT+CMGS=\"+79161234567\"\rhello\nworld\x1A") != std::string::npos);

    fresh();
    fake::respond("AT+CMGS=\"+79161234567\"", "\r\n+CMS ERROR: 304\r\n");
    CHECK(!sim808::send_sms("+79161234567", "hi"));
    CHECK(fake::log_contains("no SMS prompt: +CMS ERROR: 304"));
    CHECK(fake::sent().find("hi") == std::string::npos); // текст не ушёл

    fresh();
    fake::respond("AT+CMGS=\"+79161234567\"", "\r\n> ");
    CHECK(!sim808::send_sms("+79161234567", "hi"));
    CHECK(fake::log_contains("SMS send failed: timeout"));
}

/*
 * Проверяет разбор состояния модуля: сигнал, регистрацию в домашней сети,
 * в роуминге и её отсутствие, питание и GNSS. Параметров нет.
 */
void test_status_queries()
{
    fresh();
    CHECK(sim808::signal() == 99); // модуль молчит

    const std::pair<const char *, bool> creg_cases[] = {{"1", true}, {"5", true}, {"2", false}, {"0", false}};
    for (auto [stat, expect] : creg_cases) {
        fake::respond("AT+CREG?", std::string("\r\n+CREG: 0,") + stat + "\r\n" + OK);
        CHECK(sim808::registered() == expect);
    }

    fake::respond("AT+CBC", "\r\n+CBC: 0,85,4105\r\n" + OK);
    auto bat = sim808::battery();
    CHECK(bat && bat->percent == 85 && bat->millivolts == 4105);
    fake::respond("AT+CBC", "\r\nERROR\r\n");
    CHECK(!sim808::battery());

    fake::respond("AT+CGNSINF",
                  "\r\n+CGNSINF: 1,1,20261002054210.000,55.746320,52.022264,105.2,0.00,0.0,1,,0.9,1.2,0.8,,12,9,,,40,,\r\n" + OK);
    auto fix = sim808::gnss_read();
    CHECK(fix && fix->fix && fix->lat_e6 == 55746320 && fix->lon_e6 == 52022264 && fix->sats_used == 9);
}

/*
 * Проверяет включение модуля: если он отвечает, вся настройка проходит
 * без нажатия PWRKEY; если молчит — PWRKEY нажимается один раз,
 * и start() сообщает об ошибке. Параметров нет.
 */
void test_start()
{
    fresh();
    for (const char *c : {"AT", "ATE0", "AT+CMEE=2", "AT+CMGF=1", "AT+CSCS=\"GSM\"", "AT+CNMI=2,1,0,0,0", "AT+CGNSPWR=1"})
        fake::respond(c, OK);
    fake::respond("ATI", "\r\nSIM808 R14.18\r\n" + OK);
    fake::respond("AT+CPIN?", "\r\n+CPIN: READY\r\n" + OK);
    CHECK(sim808::start());
    CHECK(fake::pwrkey_presses() == 0);
    CHECK(fake::log_contains("module: SIM808 R14.18"));
    CHECK(fake::log_contains("SIM808 ready"));
    CHECK(!fake::log_contains("SIM not ready"));

    fresh();
    CHECK(!sim808::start());
    CHECK(fake::pwrkey_presses() == 1);
    CHECK(fake::log_contains("SIM808 does not answer"));
}

} // namespace

/*
 * Запускает все тесты AT-движка и команд SIM808 по очереди.
 * Параметров нет.
 *
 * Возвращает: 0 — все проверки прошли, 1 — хотя бы одна не прошла.
 */
int main()
{
    test_cmd_results();
    test_response_capture();
    test_unsolicited();
    test_read_sms();
    test_send_sms();
    test_status_queries();
    test_start();
    return report_failures();
}
