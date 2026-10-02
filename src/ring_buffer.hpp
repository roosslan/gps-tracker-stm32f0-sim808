/*
 * Кольцевой буфер «один писатель — один читатель» без блокировок.
 * Писатель — обработчик прерывания UART, читатель — главный цикл.
 * Индексы и данные volatile: компилятор не переставит запись данных после
 * записи индекса, а Cortex-M0 одноядерный и сам порядок не меняет.
 */
#pragma once

#include <cstddef>
#include <optional>

template <typename T, size_t N>
class RingBuffer {
    static_assert(N >= 2 && (N & (N - 1)) == 0, "размер — степень двойки: индексы маскируются");

public:
    /*
     * Кладёт элемент в конец буфера. Вызывать только со стороны писателя
     * (в прошивке — из прерывания).
     *
     * Параметры:
     *   value — элемент.
     *
     * Возвращает: true — положен; false — буфер полон, элемент отброшен.
     */
    bool push(T value)
    {
        size_t head = head_;
        size_t next = (head + 1) & (N - 1);
        if (next == tail_)
            return false;
        data_[head] = value;
        head_ = next;
        return true;
    }

    /*
     * Забирает самый старый элемент. Вызывать только со стороны читателя
     * (в прошивке — из главного цикла).
     * Параметров нет.
     *
     * Возвращает: элемент или std::nullopt, если буфер пуст.
     */
    std::optional<T> pop()
    {
        size_t tail = tail_;
        if (tail == head_)
            return std::nullopt;
        T value = data_[tail];
        tail_ = (tail + 1) & (N - 1);
        return value;
    }

    /*
     * Проверяет, пуст ли буфер.
     * Параметров нет.
     *
     * Возвращает: true, если читать нечего.
     */
    bool empty() const { return head_ == tail_; }

    /*
     * Сколько элементов помещается: на один меньше N, потому что одна ячейка
     * всегда свободна — так «полон» отличается от «пуст».
     * Параметров нет.
     *
     * Возвращает: N - 1.
     */
    static constexpr size_t capacity() { return N - 1; }

private:
    volatile T      data_[N] = {};
    volatile size_t head_ = 0; // куда пишет писатель
    volatile size_t tail_ = 0; // откуда читает читатель
};
