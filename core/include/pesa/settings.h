// FTracker/core/include/pesa/settings.h
#pragma once

#include <pesa/common.h>
#include <pesa/date.h>

#include <compare>
#include <optional>
#include <string>
#include <string_view>

namespace pesa {

    // A time on the clock with no date, like 20:00.
    struct TimeOfDay {
        int hour = 0;     // 0-23
        int minute = 0;   // 0-59

        [[nodiscard]] static TimeOfDay parse(std::string_view text);   // "8:30" or "08:30", throws Error
        [[nodiscard]] static TimeOfDay now();                          // the computer's clock
        [[nodiscard]] std::string toString() const;                    // "08:30"

        // One line gives you <, >, <=, >=, == and != (compares hour first, then minute).
        auto operator<=>(const TimeOfDay&) const = default;
    };

    // Small app-wide choices, saved in the file. Anything never set has a sensible default.
    // Created and owned by Pesa. Use it through app.settings().
    class Settings {
    public:
        explicit Settings(detail::Database& db) noexcept : db_(db) {}

        // ---- first run ----
        // False until setup has been completed once.
        [[nodiscard]] bool setupDone() const;

        // Saves the answers of the setup wizard and marks setup as done, all or nothing.
        // Empty pay = "my income varies": spending is still tracked, projections stay off.
        void completeSetup(std::optional<Money> expectedPay, std::optional<Date> expectedPayday);

        // ---- pay (an estimate; the pay history table will take over from this later) ----
        [[nodiscard]] std::optional<Money> expectedPay() const;
        [[nodiscard]] std::optional<Date>  expectedPayday() const;

        // ---- the daily job ----
        // The job runs every time Pesa opens and catches up missed days.
        [[nodiscard]] bool autoPostEnabled() const;   // default: on. Off = nothing auto-posts.
        void               setAutoPostEnabled(bool enabled);

        // Days BEFORE today always post. Today posts only once the clock is at or past this
        // time, so opening Pesa in the morning does not post spending that has not happened.
        [[nodiscard]] TimeOfDay postAfter() const;    // default: 20:00
        void                    setPostAfter(TimeOfDay time);

    private:
        [[nodiscard]] std::optional<std::string> get(std::string_view key) const;
        void set(std::string_view key, std::string_view value);
        void erase(std::string_view key);

        detail::Database& db_;
    };

} // namespace pesa