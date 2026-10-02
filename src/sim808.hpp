// Команды модуля SIM808 поверх AT-движка.
#pragma once

#include <optional>
#include <string_view>

#include "gps.hpp"
#include "sms.hpp"

namespace sim808 {

struct Sms {
    sms::Number number; // номер отправителя
    sms::Body   text;   // текст (первая строка, если текст многострочный)
};

struct Battery {
    int percent;    // 0..100
    int millivolts; // например, 4105
};

bool start();
bool alive();

bool gnss_power(bool on);
std::optional<gps::Fix> gnss_read();

void scan_unread();
bool read_sms(int index, Sms &out);
bool delete_sms(int index);
bool send_sms(std::string_view number, std::string_view text);

int signal();
bool registered();
std::optional<Battery> battery();

} // namespace sim808
