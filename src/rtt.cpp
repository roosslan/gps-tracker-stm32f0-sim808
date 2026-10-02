#include "rtt.hpp"

#include <atomic>
#include <cstring>

extern "C" {
// В .bss, а не в .data: метка появляется в RAM только после rtt::init(),
// так что программатор не найдёт недоинициализированный блок.
rtt::ControlBlock _SEGGER_RTT;
}

namespace rtt {
namespace {

char up_data[kUpBufferSize];
char down_data[kDownBufferSize];

constexpr uint32_t kModeTrim = 1; // не влезает — записать сколько можно

} // namespace

/*
 * Заполняет управляющий блок: один канал вывода "Terminal" на kUpBufferSize
 * байт и один канал ввода (не используется, но некоторые программы без него
 * не работают). Метку "SEGGER RTT" пишет последней, после остальных полей:
 * пока её нет, программатор блок не найдёт и не прочитает его наполовину
 * заполненным. Вызывать один раз при старте, до первого write().
 * Повторный вызов сбрасывает буфер, а с ним и непрочитанный текст.
 * Параметров нет.
 */
void init()
{
    ControlBlock &cb = _SEGGER_RTT;
    cb.max_up_buffers = 1;
    cb.max_down_buffers = 1;
    cb.up[0] = Buffer{"Terminal", up_data, kUpBufferSize, 0, 0, kModeTrim};
    cb.down[0] = Buffer{"Terminal", down_data, kDownBufferSize, 0, 0, kModeTrim};

    static const char id[] = "SEGGER RTT";
    std::memset(cb.id, 0, sizeof(cb.id));
    // Метку пишем с конца, чтобы она стала целой строкой только в самом конце.
    std::atomic_signal_fence(std::memory_order_seq_cst);
    for (size_t i = sizeof(id) - 1; i-- > 0;)
        reinterpret_cast<volatile char *>(cb.id)[i] = id[i];
}

/*
 * Дописывает текст в канал вывода. Не блокирует: если программатор не читает
 * и места не хватает, записывается только то, что влезает, остальное
 * отбрасывается. Одна ячейка буфера всегда остаётся пустой — так
 * программатор отличает «буфер полон» от «буфер пуст». Позиция записи
 * сдвигается только после копирования данных, поэтому программатор никогда
 * не прочитает ещё не записанные байты. Вызывать только из одного места
 * (главного цикла), не из прерываний.
 *
 * Параметры:
 *   s — текст; может быть длиннее буфера, лишнее отбросится.
 *
 * Возвращает: сколько байт записано (меньше s.size(), если не хватило места;
 * 0, если init() не вызывалась).
 */
size_t write(std::string_view s)
{
    Buffer &b = _SEGGER_RTT.up[0];
    if (!b.data || b.size == 0)
        return 0;

    uint32_t wr = b.write_offset;
    uint32_t rd = b.read_offset;
    uint32_t free_space = rd > wr ? rd - wr - 1 : b.size - (wr - rd) - 1;
    size_t n = s.size() < free_space ? s.size() : free_space;

    size_t first = n < b.size - wr ? n : b.size - wr;
    std::memcpy(b.data + wr, s.data(), first);
    std::memcpy(b.data, s.data() + first, n - first);

    // Барьер компилятора: данные должны лечь в буфер раньше, чем сдвинется
    // позиция записи. Кода не порождает, Cortex-M0 сам порядок не меняет.
    std::atomic_signal_fence(std::memory_order_seq_cst);
    b.write_offset = static_cast<uint32_t>((wr + n) % b.size);
    return n;
}

} // namespace rtt
