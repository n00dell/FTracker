// FTracker/core/src/date.cpp
#include <pesa/date.h>

#include <charconv>
#include <cstdio>
#include <ctime>

namespace pesa {

namespace chr = std::chrono;

Date Date::from(int year, int month, int day) {
    const bool plausible = year >= 1900 && year <= 9999 && month >= 1 && month <= 12 &&
                           day >= 1 && day <= 31;
    const chr::year_month_day ymd{chr::year{year}, chr::month{static_cast<unsigned>(month)},
                                  chr::day{static_cast<unsigned>(day)}};
    if (!plausible || !ymd.ok()) {   // ok() knows month lengths and leap years
        char text[48];
        std::snprintf(text, sizeof text, "%04d-%02d-%02d", year, month, day);
        throw Error(std::string("Not a real date: ") + text + ".");
    }
    return Date(chr::sys_days{ymd});
}

Date Date::parse(std::string_view s) {
    auto bad = [&] { return Error("A date must look like 2026-11-23, got '" + std::string(s) + "'."); };
    if (s.size() != 10 || s[4] != '-' || s[7] != '-') throw bad();

    auto number = [&](std::size_t pos, std::size_t len) {
        int value = 0;
        const char* first = s.data() + pos;
        const auto [end, ec] = std::from_chars(first, first + len, value);
        if (ec != std::errc{} || end != first + len) throw bad();
        return value;
    };
    return from(number(0, 4), number(5, 2), number(8, 2));
}

Date Date::today() {
    const std::time_t now = std::time(nullptr);
    std::tm local{};
#if defined(_WIN32)
    localtime_s(&local, &now);
#else
    localtime_r(&now, &local);
#endif
    return from(local.tm_year + 1900, local.tm_mon + 1, local.tm_mday);
}

int Date::year() const  { return static_cast<int>(chr::year_month_day{days_}.year()); }
int Date::month() const { return static_cast<int>(static_cast<unsigned>(chr::year_month_day{days_}.month())); }
int Date::day() const   { return static_cast<int>(static_cast<unsigned>(chr::year_month_day{days_}.day())); }

Weekday Date::weekday() const {
    // chrono counts Monday as 1 ... Sunday as 7; our Weekday starts at Mon = 0.
    return static_cast<Weekday>(chr::weekday{days_}.iso_encoding() - 1);
}

int Date::daysInMonth() const {
    const chr::year_month_day ymd{days_};
    const chr::year_month_day_last last{ymd.year() / ymd.month() / chr::last};
    return static_cast<int>(static_cast<unsigned>(last.day()));
}

Date Date::addDays(int n) const { return Date(days_ + chr::days{n}); }

int Date::daysUntil(const Date& later) const {
    return static_cast<int>((later.days_ - days_).count());
}

std::string Date::toString() const {
    char text[48];
    std::snprintf(text, sizeof text, "%04d-%02d-%02d", year(), month(), day());
    return text;
}

} // namespace pesa
