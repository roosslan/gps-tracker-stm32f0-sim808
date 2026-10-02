/*
 * Строки фиксированного размера без кучи и мелкие помощники для разбора текста.
 *
 * TextBuffer — общий интерфейс «буфер + текущая длина», через него функции
 * принимают строку любой ёмкости. Text<N> — владеющая строка с N байтами
 * памяти (включая завершающий '\0'), живёт на стеке или в статической памяти.
 * Если текст не влезает, он обрезается: переполнить буфер нельзя.
 */
#pragma once

#include <cstdarg>
#include <cstddef>
#include <optional>
#include <string_view>

#if defined(__GNUC__)
#define TEXT_PRINTF_FORMAT(fmt_index, args_index) __attribute__((format(printf, fmt_index, args_index)))
#else
#define TEXT_PRINTF_FORMAT(fmt_index, args_index)
#endif

class TextBuffer {
public:
    /* Копировать нельзя: буфер указывает на чужую память. Копируется Text<N>. */
    TextBuffer(const TextBuffer &) = delete;
    TextBuffer &operator=(const TextBuffer &) = delete;

    void clear();
    void append(std::string_view s);
    void append(char c);
    void appendf(const char *fmt, ...) TEXT_PRINTF_FORMAT(2, 3);
    void vappendf(const char *fmt, va_list ap);

    /*
     * Текущее содержимое как string_view. Указывает внутрь буфера, поэтому
     * действительно, пока буфер жив и не изменён.
     */
    std::string_view view() const { return {data_, len_}; }

    /* Содержимое как C-строка с '\0' в конце — для printf и API на C. */
    const char *c_str() const { return data_; }

    /* Длина текста без '\0'. */
    size_t size() const { return len_; }

    /* true, если текст пустой. */
    bool empty() const { return len_ == 0; }

    /* Сколько символов максимум помещается (без '\0'). */
    size_t capacity() const { return size_ - 1; }

protected:
    /*
     * Создаёт пустой буфер поверх чужой памяти. Вызывается только из Text<N>.
     *
     * Параметры:
     *   data — память под текст; data[0] должен быть '\0';
     *   size — размер памяти в байтах вместе с '\0', больше 0.
     */
    constexpr TextBuffer(char *data, size_t size) : data_(data), size_(size), len_(0) {}
    ~TextBuffer() = default;

private:
    char  *data_;
    size_t size_;
    size_t len_;
};

template <size_t N>
class Text : public TextBuffer {
    static_assert(N > 0, "нужен хотя бы байт под '\\0'");

public:
    /* Пустая строка. constexpr: глобальные Text<N> не требуют кода инициализации. */
    constexpr Text() : TextBuffer(storage_, N) {}

    /*
     * Строка с начальным содержимым.
     *
     * Параметры:
     *   s — начальный текст; то, что не влезает в N - 1 символов, обрезается.
     */
    explicit Text(std::string_view s) : Text() { append(s); }

    /*
     * Копия другой строки той же ёмкости. Свой конструктор нужен, чтобы
     * буфер копии указывал на её собственную память, а не на память оригинала.
     *
     * Параметры:
     *   other — строка, содержимое которой копируется.
     */
    Text(const Text &other) : Text() { append(other.view()); }

    /*
     * Заменяет содержимое копией другой строки той же ёмкости.
     *
     * Параметры:
     *   other — строка, содержимое которой копируется.
     *
     * Возвращает: *this.
     */
    Text &operator=(const Text &other)
    {
        if (this != &other) {
            clear();
            append(other.view());
        }
        return *this;
    }

private:
    char storage_[N] = {};
};

namespace text {

bool is_space(char c);
std::string_view trim(std::string_view s);
std::string_view head(std::string_view s, size_t n);
std::optional<int> parse_int(std::string_view s);

} // namespace text
