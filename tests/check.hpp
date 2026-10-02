// Мини-фреймворк проверок для тестов на ПК: печатает файл, строку и условие
// каждой неудачной проверки и считает их в failures.
#pragma once

#include <cstdio>
#include <string_view>

inline int failures = 0;

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
 * Печатает итог прогона тестов.
 * Параметров нет.
 *
 * Возвращает: код выхода для ctest — 0, если все проверки прошли, иначе 1.
 */
inline int report_failures()
{
    if (failures) {
        std::printf("%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("all tests passed\n");
    return 0;
}
