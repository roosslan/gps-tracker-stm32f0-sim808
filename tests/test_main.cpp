// Тесты чистой логики (строки, парсеры, кольцевой буфер, тексты ответов).
// Собираются компилятором ПК.
#include <cstdio>
#include <string_view>

#include "gps.hpp"
#include "report.hpp"
#include "ring_buffer.hpp"
#include "rtt.hpp"
#include "sms.hpp"
#include "text.hpp"

using namespace std::string_view_literals;

static int failures;

#define CHECK(cond) do { \
    if (!(cond)) { std::printf("%s:%d: CHECK(%s) failed\n", __FILE__, __LINE__, #cond); failures++; } \
} while (0)

#define CHECK_STR(a, b) do { \
    std::string_view a_ = (a), b_ = (b); \
    if (a_ != b_) { \
        std::printf("%s:%d: \"%.*s\" != \"%.*s\"\n", __FILE__, __LINE__, \
                    (int)a_.size(), a_.data(), (int)b_.size(), b_.data()); \
        failures++; } \
} while (0)

/*
 * Проверяет Text<N>: добавление строк, символов и формата, обрезку при
 * переполнении без выхода за буфер, очистку и копирование (копия должна
 * жить в своей памяти). Параметров нет.
 */
static void test_text()
{
    Text<8> t;
    CHECK(t.empty() && t.capacity() == 7);
    t.append("abc");
    t.append('d');
    t.appendf("%d", 42);
    CHECK_STR(t.view(), "abcd42");
    t.append("xyz");
    CHECK_STR(t.view(), "abcd42x");
    CHECK(t.c_str()[7] == '\0');
    t.appendf("%s", "more");
    CHECK_STR(t.view(), "abcd42x");

    Text<8> copy(t);
    t.clear();
    CHECK(t.empty() && t.c_str()[0] == '\0');
    CHECK_STR(copy.view(), "abcd42x");
    t = copy;
    CHECK_STR(t.view(), "abcd42x");

    Text<4> small("abcdef");
    CHECK_STR(small.view(), "abc");
}

/*
 * Проверяет помощники разбора: trim, head и parse_int, включая знак,
 * отсутствие цифр и переполнение int. Параметров нет.
 */
static void test_text_helpers()
{
    CHECK_STR(text::trim("  \r\nwhere \n"), "where");
    CHECK_STR(text::trim("   "), "");
    CHECK_STR(text::head("abcdef", 3), "abc");
    CHECK_STR(text::head("ab", 10), "ab");

    CHECK(text::parse_int("85,4105") == 85);
    CHECK(text::parse_int("-12") == -12);
    CHECK(text::parse_int("+7") == 7);
    CHECK(!text::parse_int(""));
    CHECK(!text::parse_int("x1"));
    CHECK(!text::parse_int(" 1"));
    CHECK(!text::parse_int("99999999999"));
}

/*
 * Проверяет кольцевой буфер: пустой, заполнение до ёмкости, отказ при
 * переполнении и порядок элементов после многократного прохода по кругу.
 * Параметров нет.
 */
static void test_ring_buffer()
{
    RingBuffer<char, 4> rb;
    CHECK(rb.empty());
    CHECK(!rb.pop());
    CHECK(rb.push('a') && rb.push('b') && rb.push('c'));
    CHECK(!rb.push('d'));
    CHECK(rb.pop() == 'a');
    CHECK(rb.push('d'));
    CHECK(rb.pop() == 'b' && rb.pop() == 'c' && rb.pop() == 'd');
    CHECK(rb.empty());

    for (int i = 0; i < 20; i++) {
        CHECK(rb.push(static_cast<char>('0' + i % 10)));
        CHECK(rb.pop() == static_cast<char>('0' + i % 10));
    }
}

/*
 * Проверяет RTT-буфер так, как его видит программатор: метку "SEGGER RTT",
 * запись текста, заполнение до отказа (одна ячейка остаётся пустой),
 * продолжение после того, как «программатор» прочитал часть, и переход
 * записи через конец буфера. Параметров нет.
 */
static void test_rtt()
{
    rtt::init();
    rtt::Buffer &up = _SEGGER_RTT.up[0];
    CHECK_STR(std::string_view(_SEGGER_RTT.id), "SEGGER RTT");
    CHECK(_SEGGER_RTT.max_up_buffers == 1 && up.size == rtt::kUpBufferSize);

    CHECK(rtt::write("hello") == 5);
    CHECK(up.write_offset == 5);
    CHECK_STR(std::string_view(up.data, 5), "hello");

    // Никто не читает: влезает ровно size - 1 байт, остальное отбрасывается
    static char big[2 * rtt::kUpBufferSize];
    for (char &c : big)
        c = 'x';
    CHECK(rtt::write(std::string_view(big, sizeof(big))) == rtt::kUpBufferSize - 1 - 5);
    CHECK(rtt::write("y") == 0);

    // «Программатор» прочитал 10 байт — места ровно на 10
    up.read_offset = 10;
    CHECK(rtt::write("0123456789ABC") == 10);
    CHECK(up.write_offset == 9);
    CHECK(up.data[rtt::kUpBufferSize - 1] == '0' && up.data[0] == '1' && up.data[8] == '9');

    // Всё прочитано: запись идёт с места остановки и снова переходит через конец
    up.read_offset = up.write_offset;
    rtt::init();
    up.write_offset = up.read_offset = rtt::kUpBufferSize - 2;
    CHECK(rtt::write("abcd") == 4);
    CHECK(up.write_offset == 2);
    CHECK(up.data[rtt::kUpBufferSize - 2] == 'a' && up.data[rtt::kUpBufferSize - 1] == 'b');
    CHECK(up.data[0] == 'c' && up.data[1] == 'd');
}

/*
 * Проверяет разбор полной строки CGNSINF с фиксом: координаты, высоту,
 * скорость, курс, HDOP, число спутников, дату и время. Параметров нет.
 */
static void test_cgnsinf_fix()
{
    auto f = gps::parse_cgnsinf(
        "+CGNSINF: 1,1,20260918093015.000,48.148598,17.107748,178.300,12.34,271.5,1,,1.2,1.6,1.0,,11,7,,,32,,");
    CHECK(f.has_value());
    if (!f)
        return;
    CHECK(f->gnss_on);
    CHECK(f->fix);
    CHECK(f->lat_e6 == 48148598);
    CHECK(f->lon_e6 == 17107748);
    CHECK(f->alt_m == 178);
    CHECK(f->speed_kmh_x10 == 123);
    CHECK(f->course_deg == 271);
    CHECK(f->hdop_x10 == 12);
    CHECK(f->sats_in_view == 11);
    CHECK(f->sats_used == 7);
    CHECK(f->utc.year == 2026 && f->utc.month == 9 && f->utc.day == 18);
    CHECK(f->utc.hour == 9 && f->utc.minute == 30 && f->utc.second == 15);
}

/*
 * Проверяет отрицательные координаты (южное и западное полушарие), включая
 * значение между -1° и 0°, короткую дробную часть ("-0.5"), отрицательную
 * высоту, "\r\n" в конце строки и пустые необязательные поля. Параметров нет.
 */
static void test_cgnsinf_southwest_and_short_fraction()
{
    auto f = gps::parse_cgnsinf("+CGNSINF: 1,1,20260101000000.000,-0.5,-122.41,-3.0,0.00,,1,,,,,,,,,,,,\r\n");
    CHECK(f && f->fix);
    if (!f)
        return;
    CHECK(f->lat_e6 == -500000);
    CHECK(f->lon_e6 == -122410000);
    CHECK(f->alt_m == -3);
    CHECK(f->speed_kmh_x10 == 0);
    CHECK(f->sats_used == 0);
}

/*
 * Проверяет строки без фикса: приёмник включён, но спутников нет; приёмник
 * выключен; модуль заявляет фикс, но координаты вне диапазона или не число.
 * Во всех случаях строка должна распознаваться, но с fix = false.
 * Параметров нет.
 */
static void test_cgnsinf_no_fix()
{
    auto f = gps::parse_cgnsinf("+CGNSINF: 1,0,19800106001555.000,,,,0.00,0.0,0,,,,,,0,0,,,,,");
    CHECK(f && f->gnss_on && !f->fix);

    f = gps::parse_cgnsinf("+CGNSINF: 0,,,,,,,,");
    CHECK(f && !f->gnss_on && !f->fix);

    f = gps::parse_cgnsinf("+CGNSINF: 1,1,20260101000000.000,95.0,10.0,0,0,0,1");
    CHECK(f && !f->fix);
    f = gps::parse_cgnsinf("+CGNSINF: 1,1,20260101000000.000,5x,10.0,0,0,0,1");
    CHECK(f && !f->fix);
}

/*
 * Проверяет, что строки, которые не являются ответом CGNSINF (OK, +CSQ,
 * префикс без полей), не принимаются за него. Параметров нет.
 */
static void test_cgnsinf_rejects_other_lines()
{
    CHECK(!gps::parse_cgnsinf("OK"));
    CHECK(!gps::parse_cgnsinf("+CGNSINF:"));
    CHECK(!gps::parse_cgnsinf("+CSQ: 18,0"));
}

/*
 * Проверяет печать координат: положительные, между -1° и 0° (минус не
 * должен теряться), крайнее значение -180° и маленькое число с ведущими
 * нулями в дробной части. Параметров нет.
 */
static void test_append_deg()
{
    Text<16> b;
    gps::append_deg(b, 55751244);
    CHECK_STR(b.view(), "55.751244");
    b.clear();
    gps::append_deg(b, -500000);
    CHECK_STR(b.view(), "-0.500000");
    b.clear();
    gps::append_deg(b, -180000000);
    CHECK_STR(b.view(), "-180.000000");
    b.clear();
    gps::append_deg(b, 7);
    CHECK_STR(b.view(), "0.000007");
}

/*
 * Проверяет разбор строк модуля о SMS: +CMTI (с разной памятью),
 * +CMGL и номер отправителя из +CMGR, включая пустой номер и номер,
 * который не влезает в sms::Number. Параметров нет.
 */
static void test_sms_lines()
{
    CHECK(sms::parse_cmti("+CMTI: \"SM\",3") == 3);
    CHECK(sms::parse_cmti("+CMTI: \"ME\",12") == 12);
    CHECK(!sms::parse_cmti("+CMGS: 5"));
    CHECK(sms::parse_cmgl("+CMGL: 7,\"REC UNREAD\",\"+79161234567\",\"\",\"26/09/28,12:00:00+12\"") == 7);

    auto num = sms::parse_cmgr_sender("+CMGR: \"REC UNREAD\",\"+79161234567\",\"\",\"26/09/28,12:00:00+12\"");
    CHECK(num && *num == "+79161234567");
    CHECK(!sms::parse_cmgr_sender("+CMGR: \"REC READ\",\"\",\"\""));
    CHECK(!sms::parse_cmgr_sender("+CMGR: \"REC READ\",\"+1234567890123456789012345678\",\"\""));
}

/*
 * Проверяет распознавание команд: все синонимы WHERE, регистр, пробелы
 * и переводы строк по краям, а также что фразы, пустой текст, рассылки
 * и склеенные слова командами не считаются. Параметров нет.
 */
static void test_sms_commands()
{
    using sms::Command;
    CHECK(sms::parse_command("where") == Command::Where);
    CHECK(sms::parse_command("  WHERE \r\n") == Command::Where);
    CHECK(sms::parse_command("Gde") == Command::Where);
    CHECK(sms::parse_command("?") == Command::Where);
    CHECK(sms::parse_command("status") == Command::Status);
    CHECK(sms::parse_command("Help") == Command::Help);
    CHECK(sms::parse_command("where are you") == Command::None);
    CHECK(sms::parse_command("") == Command::None);
    CHECK(sms::parse_command("Vash balans 100 rub") == Command::None);
    CHECK(sms::parse_command("wherewherewherewhere") == Command::None);
}

/*
 * Проверяет белый список: при пустом списке (только NULL) проходят любые
 * телефонные номера, но не буквенные и не короткие; при заполненном —
 * только номера из списка, в том числе записанные через 8 вместо +7.
 * Параметров нет.
 */
static void test_sms_whitelist()
{
    const char *const any[] = { nullptr };
    const char *const mine[] = { "+79161234567", "+421900111222", nullptr };

    CHECK(sms::sender_allowed("+79991112233", any));
    CHECK(!sms::sender_allowed("MTS", any));
    CHECK(!sms::sender_allowed("900", any));

    CHECK(sms::sender_allowed("+79161234567", mine));
    CHECK(sms::sender_allowed("89161234567", mine));
    CHECK(sms::sender_allowed("+421900111222", mine));
    CHECK(!sms::sender_allowed("+79161234568", mine));
    CHECK(!sms::sender_allowed("Beeline", mine));
}

/*
 * Проверяет текст ответа WHERE: свежий фикс, устаревший фикс с возрастом,
 * худший случай по длине (должен влезать в 160 символов), отсутствие фикса
 * и обрезку при маленьком буфере без выхода за его границу. Параметров нет.
 */
static void test_report_where()
{
    sms::Body b;
    gps::Fix f;
    f.gnss_on = f.fix = true;
    f.lat_e6 = 55751244;
    f.lon_e6 = 37618423;
    f.alt_m = 150;
    f.speed_kmh_x10 = 32;
    f.sats_used = 7;
    f.utc.hour = 12;
    f.utc.minute = 0;
    f.utc.second = 15;

    report::where(b, f, 3, true);
    CHECK_STR(b.view(), "https://maps.google.com/?q=55.751244,37.618423\n"
                        "12:00:15 UTC spd 3.2km/h alt 150m sats 7");

    report::where(b, f, 300, true);
    CHECK(b.view().starts_with("No fix now, last one 5min ago\n"));
    CHECK(b.size() <= sms::kTextMax);

    // Худший случай по длине: южное/западное полушарие, большие числа
    f.lat_e6 = -89999999;
    f.lon_e6 = -179999999;
    f.alt_m = -12345;
    f.speed_kmh_x10 = 99999;
    f.sats_used = 99;
    report::where(b, f, 200000, true);
    CHECK(b.size() <= sms::kTextMax);
    CHECK(b.view().find("sats 99") != std::string_view::npos); // не обрезано

    report::where(b, std::nullopt, 0, true);
    CHECK(b.view().starts_with("No GPS fix yet"));

    Text<20> small;
    report::where(small, f, 0, true);
    CHECK(small.size() == 19);
}

/*
 * Проверяет текст ответа STATUS при нормальных показателях и при
 * неизвестных (нет сигнала, сети и данных о питании), а также что ответ
 * HELP влезает в одну SMS. Параметров нет.
 */
static void test_report_status()
{
    sms::Body b;
    report::Status st{18, true, 85, 4105, true, true, 5, 7, 7980};
    report::status(b, st);
    CHECK_STR(b.view(), "GSM 18/31 reg\nPower 85% 4.10V\nGPS on, fix, sats 7\nUptime 2h");

    report::Status bad{99, false, -1, 0, false, false, 0, 0, 5};
    report::status(b, bad);
    CHECK_STR(b.view(), "GSM ? NOT reg\nGPS OFF, no fix yet\nUptime 5s");

    report::help(b);
    CHECK(!b.empty() && b.size() <= sms::kTextMax);
}

/*
 * Запускает все тесты по очереди. Каждая неудачная проверка печатает файл,
 * строку и условие.
 * Параметров нет.
 *
 * Возвращает: 0 — все проверки прошли, 1 — хотя бы одна не прошла
 * (так ctest понимает результат).
 */
int main()
{
    test_text();
    test_text_helpers();
    test_ring_buffer();
    test_rtt();
    test_cgnsinf_fix();
    test_cgnsinf_southwest_and_short_fraction();
    test_cgnsinf_no_fix();
    test_cgnsinf_rejects_other_lines();
    test_append_deg();
    test_sms_lines();
    test_sms_commands();
    test_sms_whitelist();
    test_report_where();
    test_report_status();

    if (failures) {
        std::printf("%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("all tests passed\n");
    return 0;
}
