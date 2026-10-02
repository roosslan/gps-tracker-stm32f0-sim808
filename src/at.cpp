#include "at.hpp"

#include <array>

#include "board.hpp"
#include "sms.hpp"
#include "uart.hpp"

namespace at {
namespace {

constexpr size_t kLineMax = 200;
constexpr size_t kSmsQueue = 8;

/*
 * Очередь номеров ячеек SIM с необработанными SMS. Небольшая и без повторов:
 * одна и та же SMS может прийти и через +CMTI, и через периодический AT+CMGL.
 */
class SmsQueue {
public:
    /*
     * Добавляет номер ячейки в конец очереди, если его там ещё нет.
     *
     * Параметры:
     *   index — номер ячейки памяти SIM.
     *
     * Возвращает: false — очередь полна и номер отброшен (SMS останется
     * непрочитанной в памяти SIM, её подберёт следующий AT+CMGL);
     * true — номер в очереди (в том числе если уже был там).
     */
    bool push(int index)
    {
        for (size_t i = 0; i < count_; i++)
            if (items_[i] == index)
                return true;
        if (count_ == items_.size())
            return false;
        items_[count_++] = index;
        return true;
    }

    /*
     * Достаёт самый старый номер из очереди.
     * Параметров нет.
     *
     * Возвращает: номер ячейки или std::nullopt, если очередь пуста.
     */
    std::optional<int> pop()
    {
        if (!count_)
            return std::nullopt;
        int first = items_[0];
        for (size_t i = 1; i < count_; i++)
            items_[i - 1] = items_[i];
        count_--;
        return first;
    }

private:
    std::array<int, kSmsQueue> items_{};
    size_t count_ = 0;
};

// Строка, которая сейчас собирается из байтов UART
Text<kLineMax> line;

// Состояние текущей команды
bool cmd_active;
std::optional<Result> cmd_result;  // пусто — итога ещё нет
TextBuffer *resp;                  // куда складывать ответ, может быть nullptr
std::string_view want_prefix;      // at::query: ловим только эту строку
bool prompt_wanted, prompt_seen;

// Строка сразу после +CMGR:/+CMGL: — это текст SMS, даже если там написано "OK".
bool data_line_next;

Text<32> error_text;
bool power_down;
SmsQueue sms_queue;

/*
 * Ставит номер ячейки новой SMS в очередь на обработку. Если очередь полна,
 * пишет в лог, что SMS будет подобрана периодическим сканированием.
 *
 * Параметры:
 *   index — номер ячейки памяти SIM, где лежит SMS.
 */
void queue_sms(int index)
{
    if (!sms_queue.push(index))
        logger::print("SMS queue full, index %d will be picked up by the periodic scan", index);
}

/*
 * Сохраняет строку ответа текущей команды в буфер вызывающего. В режиме
 * at::query (want_prefix задан) сохраняется только первая строка,
 * начинающаяся с want_prefix. Иначе все строки склеиваются через '\n'.
 * Что не влезает в буфер, обрезается. Если команды нет или буфер не задан,
 * ничего не делает.
 *
 * Параметры:
 *   s — строка от модуля без "\r\n".
 */
void capture(std::string_view s)
{
    if (!cmd_active || !resp)
        return;
    if (!want_prefix.empty()) {
        if (resp->empty() && s.starts_with(want_prefix))
            resp->append(s);
        return;
    }
    if (!resp->empty())
        resp->append('\n');
    resp->append(s);
}

/*
 * Разбирает одну полную строку от модуля и решает, что это:
 *   - текст SMS (строка сразу после +CMGR:/+CMGL:) — сохраняется в ответ,
 *     даже если это "OK" или "ERROR";
 *   - +CMTI или +CMGL — номер ячейки SMS ставится в очередь;
 *   - NORMAL/UNDER-VOLTAGE POWER DOWN — модуль выключается, выставляется флаг;
 *   - OK / ERROR / +CME ERROR / +CMS ERROR во время команды — итог команды;
 *   - любая другая строка во время команды — часть её ответа;
 *   - строка без команды (RDY, SMS Ready, ...) — просто пишется в лог.
 *
 * Параметры:
 *   s — строка без "\r\n" и ведущих пробелов, не пустая.
 */
void handle_line(std::string_view s)
{
    if (data_line_next) {
        data_line_next = false;
        capture(s);
        return;
    }
    if (auto idx = sms::parse_cmti(s)) {
        queue_sms(*idx);
        return;
    }
    if (auto idx = sms::parse_cmgl(s)) {
        queue_sms(*idx);
        data_line_next = true;
        return;
    }
    if (s.starts_with("+CMGR:")) {
        data_line_next = true;
        capture(s);
        return;
    }
    if (s.starts_with("NORMAL POWER DOWN") || s.starts_with("UNDER-VOLTAGE POWER DOWN")) {
        power_down = true;
        logger::print("module: %.*s", static_cast<int>(s.size()), s.data());
        return;
    }

    if (cmd_active) {
        if (s == "OK") {
            cmd_result = Result::Ok;
        } else if (s == "ERROR" || s.starts_with("+CME ERROR") || s.starts_with("+CMS ERROR")) {
            error_text.clear();
            error_text.append(s);
            cmd_result = Result::Error;
        } else {
            capture(s);
        }
        return;
    }

    // RDY, Call Ready, SMS Ready, +CPIN: READY, UNDER-VOLTAGE WARNNING и т.п.
    logger::print("module: %.*s", static_cast<int>(s.size()), s.data());
}

/*
 * Начинает новую AT-команду: сначала разбирает всё, что пришло раньше
 * (это не ответ на новую команду), затем сбрасывает итог, привязывает буфер
 * ответа и очищает текст последней ошибки.
 *
 * Параметры:
 *   r — буфер для строк ответа или nullptr, если ответ не нужен;
 *       прежнее содержимое стирается.
 */
void begin(TextBuffer *r)
{
    poll();
    cmd_active = true;
    cmd_result.reset();
    resp = r;
    if (r)
        r->clear();
    want_prefix = {};
    error_text.clear();
}

/*
 * Отправляет команду модулю: текст команды и '\r' в конце.
 *
 * Параметры:
 *   command — команда без "\r", например "AT+CMGF=1".
 */
void send(std::string_view command)
{
    sim_uart::write(command);
    sim_uart::put('\r');
}

} // namespace

/*
 * Забирает все байты, накопившиеся в приёмном буфере UART, собирает из них
 * строки по '\r'/'\n' и передаёт каждую в handle_line(). Пустые строки
 * и ведущие пробелы отбрасываются, строки длиннее 199 символов обрезаются.
 * Отдельно ловит приглашение "> " от AT+CMGS: оно приходит без перевода
 * строки, поэтому распознаётся по символу '>' в начале строки, пока его
 * ждёт cmd_prompt(). Не блокирует. Звать как можно чаще из главного
 * цикла, иначе уведомления о новых SMS будут обработаны с задержкой,
 * а при переполнении буфера UART (511 байт) потеряются байты.
 * Параметров нет.
 */
void poll()
{
    while (auto c = sim_uart::get()) {
        if (*c == '\r' || *c == '\n') {
            if (!line.empty()) {
                handle_line(line.view());
                line.clear();
            }
        } else if (*c == '>' && line.empty() && prompt_wanted) {
            prompt_seen = true; // "> " приходит без перевода строки
        } else if (line.empty() && *c == ' ') {
            // пробел после '>' и прочие ведущие пробелы
        } else {
            line.append(*c);
        }
    }
}

/*
 * Отправляет модулю байты как есть, без "\r" в конце. Нужна для текста
 * SMS после приглашения "> ".
 *
 * Параметры:
 *   s — отправляемый текст.
 */
void write(std::string_view s)
{
    sim_uart::write(s);
}

/*
 * Отправляет модулю один байт. Нужна для управляющих символов:
 * Ctrl+Z (0x1A) — отправить SMS, ESC (0x1B) — отменить ввод.
 *
 * Параметры:
 *   c — байт для отправки.
 */
void write(char c)
{
    sim_uart::put(c);
}

/*
 * Ждёт итог открытой команды (OK или ERROR), разбирая всё, что приходит,
 * и кормя сторожевой таймер. После возврата команда считается завершённой.
 * Ответ складывается в буфер, заданный при начале команды.
 *
 * Параметры:
 *   timeout_ms — сколько ждать итога, мс.
 *
 * Возвращает: Result::Ok, Result::Error (текст ошибки — в last_error())
 * или Result::Timeout, если итог не пришёл за timeout_ms.
 */
Result wait_result(uint32_t timeout_ms)
{
    uint32_t start = board::millis();
    while (!cmd_result && board::millis() - start < timeout_ms) {
        board::watchdog_kick();
        poll();
    }
    Result res = cmd_result.value_or(Result::Timeout);
    cmd_active = false;
    want_prefix = {};
    resp = nullptr;
    if (res == Result::Timeout) {
        error_text.clear();
        error_text.append("timeout");
    }
    return res;
}

/*
 * Отправляет AT-команду (добавляет "\r") и ждёт итог. Строки между
 * командой и итогом (ответ модуля) склеиваются через '\n' в resp.
 * Уведомления +CMTI, пришедшие в это время, в ответ не попадают,
 * а ставятся в очередь SMS.
 *
 * Параметры:
 *   command    — команда без "\r", например "AT+CMGF=1";
 *   timeout_ms — сколько ждать итога, мс;
 *   resp       — буфер для строк ответа или nullptr, если ответ не нужен;
 *                прежнее содержимое стирается, длинный ответ обрезается.
 *
 * Возвращает: Result::Ok, Result::Error или Result::Timeout (см. wait_result).
 */
Result cmd(std::string_view command, uint32_t timeout_ms, TextBuffer *r)
{
    begin(r);
    send(command);
    return wait_result(timeout_ms);
}

/*
 * Отправляет команду, после которой модуль ждёт данные (AT+CMGS="номер"),
 * и дожидается приглашения "> ". Команда при этом остаётся открытой:
 * вызывающий дальше пишет данные через write(), завершает их Ctrl+Z
 * через write('\x1A') и получает итог через wait_result().
 * Если модуль вместо приглашения ответил ERROR, ожидание прекращается сразу.
 *
 * Параметры:
 *   command    — команда без "\r";
 *   timeout_ms — сколько ждать приглашения, мс.
 *
 * Возвращает: true — приглашение получено, можно писать данные;
 * false — ERROR или таймаут. В этом случае команда всё равно открыта, и её
 * нужно закрыть через wait_result() (при таймауте — сначала отправив ESC).
 */
bool cmd_prompt(std::string_view command, uint32_t timeout_ms)
{
    begin(nullptr);
    prompt_wanted = true;
    prompt_seen = false;
    send(command);

    uint32_t start = board::millis();
    while (!prompt_seen && !cmd_result && board::millis() - start < timeout_ms) {
        board::watchdog_kick();
        poll();
    }
    bool ok = prompt_seen;
    prompt_wanted = prompt_seen = false;
    return ok;
}

/*
 * Отправляет команду-запрос и возвращает одну строку ответа, начинающуюся
 * с prefix: например, для "AT+CSQ" с префиксом "+CSQ:" вернётся
 * "+CSQ: 18,0". Остальные строки ответа (включая эхо команды) игнорируются.
 *
 * Параметры:
 *   command    — команда без "\r";
 *   prefix     — начало нужной строки ответа, не пустое;
 *   out        — куда записать строку; при неудаче там может остаться пустая
 *                строка; длинная строка обрезается;
 *   timeout_ms — сколько ждать итога, мс.
 *
 * Возвращает: true — модуль ответил OK и строка с prefix была;
 * false — ERROR, таймаут или нужной строки не было.
 */
bool query(std::string_view command, std::string_view prefix, TextBuffer &out, uint32_t timeout_ms)
{
    begin(&out);
    want_prefix = prefix;
    send(command);
    return wait_result(timeout_ms) == Result::Ok && !out.empty();
}

/*
 * Текст ошибки последней команды — для лога: строка модуля ("ERROR",
 * "+CMS ERROR: 500", "+CME ERROR: SIM not inserted", обрезается до
 * 31 символа) или "timeout". Если последняя команда прошла успешно,
 * возвращается пустая строка.
 * Параметров нет.
 *
 * Возвращает: string_view на внутренний буфер; он перезаписывается
 * следующей командой.
 */
std::string_view last_error()
{
    return error_text.view();
}

/*
 * Достаёт из очереди номер ячейки самой старой необработанной SMS.
 * Очередь пополняется в poll() из уведомлений +CMTI и ответов AT+CMGL.
 * Параметров нет.
 *
 * Возвращает: номер ячейки или std::nullopt, если очередь пуста.
 */
std::optional<int> pop_sms()
{
    return sms_queue.pop();
}

/*
 * Проверяет, сообщал ли модуль о выключении ("NORMAL POWER DOWN" —
 * например, нажата его кнопка; "UNDER-VOLTAGE POWER DOWN" — просело
 * питание). Флаг сбрасывается при чтении, так что true вернётся один раз
 * на каждое такое сообщение.
 * Параметров нет.
 *
 * Возвращает: true, если модуль выключился с момента прошлого вызова.
 */
bool take_power_down()
{
    bool v = power_down;
    power_down = false;
    return v;
}

} // namespace at
