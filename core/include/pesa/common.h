// FTracker/core/include/pesa/common.h
// Small shared types used by every feature.
#pragma once

#include <cstdint>
#include <initializer_list>
#include <stdexcept>
#include <string>
#include <string_view>

namespace pesa {

// Every error the core reports is a pesa::Error, so callers can catch just that.
class Error : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

using Money      = std::int64_t;   // whole KES, no decimals
using CategoryId = std::int64_t;
using ItemId     = std::int64_t;
using EntryId    = std::int64_t;
using RuleId     = std::int64_t;

// Declared here (not defined) so feature classes can hold a reference to the
// database without this public header ever including SQLite.
namespace detail { class Database; }

// What a list() call should return. A row is either in use, switched off
// ("inactive": paused, can be flipped back) or deleted (gone from normal lists,
// kept in the file so history still makes sense, and its name can be reused).
enum class Visibility {
    ActiveOnly,        // normal use: not deleted and switched on
    IncludeInactive,   // also the switched-off ones (still not deleted)
    DeletedOnly,       // the "trash": what can be restored
};

enum class Weekday : int { Mon = 0, Tue, Wed, Thu, Fri, Sat, Sun };

// A set of weekdays, stored as 7 bits in one number (Mon = bit 0 ... Sun = bit 6).
// Callers never touch the bits: they use add/remove/contains.
class WeekdaySet {
public:
    constexpr WeekdaySet() = default;
    constexpr WeekdaySet(std::initializer_list<Weekday> days) {
        for (Weekday d : days) add(d);
    }

    static constexpr WeekdaySet fromBits(unsigned bits) {
        WeekdaySet s;
        s.bits_ = bits & 0x7Fu;
        return s;
    }
    static constexpr WeekdaySet everyDay() { return fromBits(0b1111111); }
    static constexpr WeekdaySet workDays() { return fromBits(0b0011111); }   // Mon-Fri
    static constexpr WeekdaySet weekend()  { return fromBits(0b1100000); }   // Sat, Sun

    // "mon,wed,fri", "daily", "weekdays", "weekend", "none". Throws Error if unclear.
    static WeekdaySet parse(std::string_view text);

    constexpr void add(Weekday d)    { bits_ |= bit(d); }
    constexpr void remove(Weekday d) { bits_ &= ~bit(d); }
    [[nodiscard]] constexpr bool contains(Weekday d) const { return (bits_ & bit(d)) != 0; }
    [[nodiscard]] constexpr bool empty() const { return bits_ == 0; }
    [[nodiscard]] constexpr unsigned bits() const { return bits_; }

    [[nodiscard]] std::string toString() const;   // "Every day", "Mon-Fri", "Mon,Wed"

    constexpr bool operator==(const WeekdaySet&) const = default;

private:
    static constexpr unsigned bit(Weekday d) { return 1u << static_cast<unsigned>(d); }
    unsigned bits_ = 0;
};

} // namespace pesa
