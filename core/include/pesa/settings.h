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

        auto operator<=>(const TimeOfDay&) const = default;
    };

    // Regular: a salary with an expected pay and payday. Irregular: freelance or project
    // income, so there is no payday to track, no auto-pay and no "late" state.
    enum class IncomeMode : int { Regular = 0, Irregular = 1 };

    // The answers of the first-run wizard. Use Pesa::completeSetup, which also opens the
    // first period; Settings::completeSetup only saves the settings part.
    struct SetupAnswers {
        IncomeMode           mode = IncomeMode::Regular;
        std::optional<Money> pay;               // Regular: required. Irregular: ignored.
        std::optional<Date>  payday;            // Regular: required. Irregular: ignored.
        Money                startingBalance = 0;   // what you have right now (becomes earned money)
    };

    // Small app-wide choices, saved in the file. Anything never set has a sensible default.
    // Created and owned by Pesa. Use it through app.settings().
    class Settings {
    public:
        explicit Settings(detail::Database& db) noexcept : db_(db) {}

        // ---- first run ----
        [[nodiscard]] bool setupDone() const;
        // Saves mode and pay, and marks setup as done, all or nothing. Throws if a regular
        // income has no pay or payday. (The payday itself lives on the period.)
        void completeSetup(const SetupAnswers& answers);

        [[nodiscard]] IncomeMode           incomeMode() const;   // an older file without it: pay set = regular
        [[nodiscard]] std::optional<Money> expectedPay() const;  // until pay history takes over

        // ---- the daily job ----
        [[nodiscard]] bool autoPostEnabled() const;   // default: on. Off = nothing auto-posts.
        void               setAutoPostEnabled(bool enabled);

        // Days BEFORE today always post. Today posts only once the clock is at or past this.
        [[nodiscard]] TimeOfDay postAfter() const;    // default: 20:00
        void                    setPostAfter(TimeOfDay time);

        // ---- getting paid ----
        // On: when the expected payday has passed, Pesa closes the period itself (regular
        // income only). Off: only the "I got paid" button does.
        [[nodiscard]] bool autoPayEnabled() const;    // default: on
        void               setAutoPayEnabled(bool enabled);

    private:
        [[nodiscard]] std::optional<std::string> get(std::string_view key) const;
        void set(std::string_view key, std::string_view value);
        void erase(std::string_view key);

        detail::Database& db_;
    };

} // namespace pesa