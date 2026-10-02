// Настройки трекера. Меняй здесь и пересобирай.
#pragma once

#include <cstddef>
#include <cstdint>

/*
 * Номера, которым трекер отвечает (CFG_ALLOWED_NUMBERS), лежат в отдельном
 * файле src/allowed_numbers.h, который не попадает в git. Шаблон —
 * src/allowed_numbers.example.h. Без этого файла прошивка соберётся
 * с предупреждением и будет отвечать любому номеру.
 *
 * Напрямую он не подключается: при каждой сборке CMake копирует его
 * в build/.../generated/allowed_numbers_build.h (см. cmake/gen_allowed_numbers.cmake).
 * Так сборка замечает, что файл появился или пропал, — обычный #include
 * отсутствующего файла в зависимости не попадает.
 */
#include "allowed_numbers_build.h"

namespace config {

// Белый список номеров; nullptr в конце (NULL из allowed_numbers.h) пропускается.
inline constexpr const char *kAllowedNumbers[] = { CFG_ALLOWED_NUMBERS };

// Скорость UART к SIM808. Модуль по умолчанию подстраивается сам (autobaud).
inline constexpr unsigned kSimBaud = 115200;
// Скорость отладочного лога на PA2.
inline constexpr unsigned kLogBaud = 115200;

// Как часто опрашивать GNSS, мс.
inline constexpr uint32_t kGnssPollMs = 5000;
// Как часто перечитывать непрочитанные SMS (на случай пропущенного +CMTI)
// и проверять регистрацию в сети, мс.
inline constexpr uint32_t kHousekeepingMs = 10u * 60u * 1000u;
// После скольких неудачных опросов GNSS подряд модуль считается пропавшим.
inline constexpr unsigned kMaxPollFailures = 5;

// Не больше стольких ответных SMS в час — защита баланса от зацикливания.
inline constexpr unsigned kMaxRepliesPerHour = 20;

} // namespace config
