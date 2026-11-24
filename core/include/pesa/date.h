// FTracker/core/include/pesa/date.h
#pragma once

#include <pesa/common.h>

#include <chrono>
#include <compare>
#include <string>
#include <string_view>

namespace pesa {

    // A real calendar date (no time of day). It cannot hold "2026-02-30": invalid dates
    // are rejected when you build one. In the database it is stored as "YYYY-MM-DD" text.
    class Date {
    public:
        Date() = default;   // 1970-01-01, so structs that contain a Date can be default-built

        [[nodiscard]] static Date from(int year, int month, int day);   // throws Error if not real
        [[nodiscard]] static Date parse(std::string_view iso);          // "2026-11-23", throws Error
        // The computer's date. The core never calls this by itself: whoever needs "today"
        // is handed it, so tests can pretend it is any day they like.
        [[nodiscard]] static Date today();

        [[nodiscard]] int year() const;
        [[nodiscard]] int month() const;     // 1-12
        [[nodiscard]] int day() const;       // 1-31
        [[nodiscard]] Weekday weekday() const;
        [[nodiscard]] int daysInMonth() const;   // 28-31, knows leap years

        [[nodiscard]] Date addDays(int n) const;          // n may be negative
        [[nodiscard]] Date nextDay() const     { return addDays(1); }
        [[nodiscard]] Date previousDay() const { return addDays(-1); }
        [[nodiscard]] int  daysUntil(const Date& later) const;   // positive if `later` is after this

        [[nodiscard]] std::string toString() const;       // "2026-11-23"

        // One line gives you <, >, <=, >=, == and != (C++20 "spaceship" operator).
        auto operator<=>(const Date&) const = default;

    private:
        explicit Date(std::chrono::sys_days d) : days_(d) {}
        std::chrono::sys_days days_{};   // number of whole days since 1970-01-01
    };

} // namespace pesa
