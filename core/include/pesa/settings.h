// FTracker/core/include/pesa/settings.h
#pragma once

#include <pesa/common.h>
#include <pesa/date.h>

#include <optional>
#include <string>
#include <string_view>

namespace pesa {

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
        // The job runs every time Pesa opens (and catches up missed days), so there is no
        // time of day to choose. This switch is the only control: off = nothing auto-posts.
        [[nodiscard]] bool autoPostEnabled() const;   // default: on
        void               setAutoPostEnabled(bool enabled);

    private:
        [[nodiscard]] std::optional<std::string> get(std::string_view key) const;
        void set(std::string_view key, std::string_view value);
        void erase(std::string_view key);

        detail::Database& db_;
    };

} // namespace pesa