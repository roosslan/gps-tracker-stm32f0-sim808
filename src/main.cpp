/*
 * GPS-трекер на STM32F0DISCOVERY + SIM808.
 * Присылаешь SMS "where" — получаешь ссылку на карту с координатами.
 */
#include <optional>

#include "at.hpp"
#include "board.hpp"
#include "config.hpp"
#include "gps.hpp"
#include "report.hpp"
#include "sim808.hpp"
#include "sms.hpp"
#include "uart.hpp"

namespace {

/*
 * Состояние и логика трекера, пока модуль работает: последний фикс GPS,
 * лимит ответов и обработка входящих SMS.
 */
class Tracker {
public:
    void run();

private:
    void poll_gnss();
    bool reply_allowed();
    void handle_sms(int index);
    void update_leds() const;
    uint32_t fix_age_s() const;
    bool fix_is_fresh() const;

    // GNSS
    bool gnss_on_ = false;
    std::optional<gps::Fix> last_fix_;
    uint32_t last_fix_ms_ = 0;
    uint32_t last_poll_ms_ = 0;
    unsigned poll_failures_ = 0;

    // Лимит ответов: окно в один час
    uint32_t replies_window_start_ms_ = 0;
    unsigned replies_sent_ = 0;
};

/*
 * Сколько секунд прошло с последнего фикса GPS. Имеет смысл, только если
 * фикс был (last_fix_ не пуст). Параметров нет.
 *
 * Возвращает: возраст фикса в секундах.
 */
uint32_t Tracker::fix_age_s() const
{
    return (board::millis() - last_fix_ms_) / 1000u;
}

/*
 * Проверяет, есть ли свежий фикс — моложе report::kFreshS секунд.
 * Параметров нет.
 *
 * Возвращает: true, если координаты актуальны.
 */
bool Tracker::fix_is_fresh() const
{
    return last_fix_ && fix_age_s() < report::kFreshS;
}

/*
 * Опрашивает GNSS-приёмник и обновляет состояние:
 *   - запоминает время опроса (от него отсчитывается следующий);
 *   - если модуль не ответил, увеличивает счётчик неудач (после
 *     config::kMaxPollFailures подряд run() перезапускает модуль);
 *   - если приёмник выключен (например, модуль перезагрузился сам),
 *     снова включает его;
 *   - если есть фикс, сохраняет его как последний известный.
 * В лог пишет только появление фикса после перерыва, чтобы не засорять
 * его каждые 5 с. Параметров нет.
 */
void Tracker::poll_gnss()
{
    last_poll_ms_ = board::millis();
    auto f = sim808::gnss_read();
    if (!f) {
        poll_failures_++;
        return;
    }
    poll_failures_ = 0;

    if (!f->gnss_on) {
        if (gnss_on_)
            logger::print("GNSS turned off by itself, turning it on");
        sim808::gnss_power(true);
    }
    gnss_on_ = f->gnss_on;

    if (f->fix) {
        if (!fix_is_fresh())
            logger::print("GPS fix, sats %u", f->sats_used);
        last_fix_ = *f;
        last_fix_ms_ = board::millis();
    }
}

/*
 * Проверяет лимит ответных SMS: не больше config::kMaxRepliesPerHour за окно
 * в один час. Окно фиксированное: отсчитывается от первого запроса после
 * окончания предыдущего окна. Сама функция счётчик не увеличивает — это
 * делается только после успешной отправки. Нужна, чтобы зацикливание
 * (например, переписка с автоответчиком) не съело баланс.
 * Параметров нет.
 *
 * Возвращает: true — ответить можно.
 */
bool Tracker::reply_allowed()
{
    uint32_t now = board::millis();
    if (now - replies_window_start_ms_ >= 3600u * 1000u) {
        replies_window_start_ms_ = now;
        replies_sent_ = 0;
    }
    return replies_sent_ < config::kMaxRepliesPerHour;
}

/*
 * Обрабатывает одну входящую SMS целиком:
 *   1. читает её и сразу удаляет из памяти SIM (даже если прочитать не
 *      удалось, чтобы битая ячейка не застряла навсегда);
 *   2. проверяет отправителя по белому списку из allowed_numbers.h;
 *   3. распознаёт команду; на не-команды не отвечает;
 *   4. проверяет лимит ответов в час;
 *   5. собирает ответ (WHERE — со свежим опросом GPS, STATUS — с опросом
 *      сигнала, сети и питания) и отправляет его отправителю.
 * Все решения пишутся в лог. На время отправки горит синий светодиод.
 *
 * Параметры:
 *   index — номер ячейки памяти SIM, где лежит SMS.
 */
void Tracker::handle_sms(int index)
{
    sim808::Sms msg;
    bool ok = sim808::read_sms(index, msg);
    sim808::delete_sms(index);
    if (!ok)
        return;

    logger::print("SMS #%d from %s: %s", index, msg.number.c_str(), msg.text.c_str());
    if (!sms::sender_allowed(msg.number.view(), config::kAllowedNumbers)) {
        logger::print("sender is not in src/allowed_numbers.h, ignored");
        return;
    }
    sms::Command command = sms::parse_command(msg.text.view());
    if (command == sms::Command::None) {
        logger::print("not a command, ignored");
        return;
    }
    if (!reply_allowed()) {
        logger::print("reply limit reached (%u per hour), ignored", config::kMaxRepliesPerHour);
        return;
    }

    sms::Body reply;
    switch (command) {
    case sms::Command::Where:
        poll_gnss();
        report::where(reply, last_fix_, fix_age_s(), gnss_on_);
        break;
    case sms::Command::Status: {
        report::Status st{
            .csq = sim808::signal(),
            .registered = sim808::registered(),
            .gnss_on = gnss_on_,
            .have_fix = last_fix_.has_value(),
            .fix_age_s = fix_age_s(),
            .sats_used = last_fix_ ? last_fix_->sats_used : uint8_t{0},
            .uptime_s = board::millis() / 1000u,
        };
        if (auto bat = sim808::battery()) {
            st.battery_pct = bat->percent;
            st.battery_mv = bat->millivolts;
        }
        report::status(reply, st);
        break;
    }
    default:
        report::help(reply);
        break;
    }

    board::led_blue(true);
    bool sent = sim808::send_sms(msg.number.view(), reply.view());
    board::led_blue(false);
    if (sent)
        replies_sent_++;
    logger::print("reply to %s %s", msg.number.c_str(), sent ? "sent" : "FAILED");
}

/*
 * Обновляет зелёный светодиод по состоянию GPS: при свежем фиксе он мигает
 * раз в секунду (0.5 с горит, 0.5 с нет), без фикса — короткая вспышка
 * 80 мс раз в 2 с. Мигание считается от board::millis(), поэтому функцию
 * нужно просто часто вызывать из главного цикла. Параметров нет.
 */
void Tracker::update_leds() const
{
    uint32_t now = board::millis();
    board::led_green(fix_is_fresh() ? (now % 1000u) < 500u : (now % 2000u) < 80u);
}

/*
 * Главный рабочий цикл трекера, пока модуль жив:
 *   - при входе запрашивает SMS, пришедшие, пока трекер был выключен;
 *   - постоянно разбирает данные от модуля и обновляет светодиоды;
 *   - обрабатывает все SMS из очереди;
 *   - раз в config::kGnssPollMs опрашивает GPS;
 *   - раз в config::kHousekeepingMs проверяет регистрацию в сети (пишет
 *     в лог её потерю и восстановление) и перечитывает непрочитанные SMS;
 *   - между делами спит на WFI до следующего прерывания.
 * Выходит, если модуль сообщил о выключении или config::kMaxPollFailures раз
 * подряд не ответил на опрос GPS, — тогда main() перезапустит модуль.
 * Параметров нет.
 */
void Tracker::run()
{
    uint32_t last_housekeeping = board::millis();
    bool was_registered = false;
    poll_failures_ = 0;

    sim808::scan_unread();

    for (;;) {
        board::watchdog_kick();
        at::poll();
        update_leds();

        if (at::take_power_down()) {
            logger::print("module powered down");
            return;
        }

        while (auto index = at::pop_sms())
            handle_sms(*index);

        uint32_t now = board::millis();
        if (now - last_poll_ms_ >= config::kGnssPollMs) {
            poll_gnss();
            if (poll_failures_ >= config::kMaxPollFailures) {
                logger::print("module stopped answering");
                return;
            }
        }

        if (now - last_housekeeping >= config::kHousekeepingMs) {
            last_housekeeping = now;
            bool reg = sim808::registered();
            if (reg != was_registered)
                logger::print(reg ? "GSM network registered" : "GSM network lost");
            was_registered = reg;
            sim808::scan_unread();
        }
        board::wait_for_interrupt();
    }
}

/*
 * Проверяет, задан ли в allowed_numbers.h хоть один номер.
 * Параметров нет.
 *
 * Возвращает: true, если белый список пуст и трекер отвечает любому номеру.
 */
bool whitelist_empty()
{
    for (const char *n : config::kAllowedNumbers)
        if (n)
            return false;
    return true;
}

Tracker tracker;

} // namespace

/*
 * Точка входа прошивки. Настраивает плату и оба UART, пишет в лог причину
 * сброса и предупреждение, если белый список номеров пуст. Дальше
 * бесконечно: включает и настраивает модуль, работает в Tracker::run(),
 * пока модуль жив, а при сбое 10 с мигает синим светодиодом и пробует снова.
 * Параметров нет, никогда не возвращается.
 */
int main()
{
    board::init();
    logger::init(config::kLogBaud);
    sim_uart::init(config::kSimBaud);

    logger::print("gps-tracker start, reset: %s", board::reset_cause_name(board::reset_cause()));
    if (whitelist_empty())
        logger::print("WARNING: src/allowed_numbers.h is empty or missing, anyone can request the location");

    for (;;) {
        board::led_blue(true);
        bool up = sim808::start();
        board::led_blue(false);
        if (up)
            tracker.run();

        // Модуль не отвечает или пропал — пауза и заново.
        for (int i = 0; i < 10; i++) {
            board::led_blue(true);
            board::delay_ms(100);
            board::led_blue(false);
            board::delay_ms(900);
        }
    }
}
