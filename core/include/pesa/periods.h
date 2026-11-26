// FTracker/core/include/pesa/periods.h
#pragma once

#include <pesa/common.h>
#include <pesa/date.h>
#include <pesa/settings.h>

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace pesa {
    // A period runs from one "I got paid" to the next: start <= date < end.
    // An entry dated on the pay day belongs to the NEW period, so the leftover of the old
    // one is exactly "income minus earned spending before that day".
    struct Period {
        PeriodId id = 0;
        Date start;
        std::optional<Date> end; // empty = the open period
        std::optional<Date> expectedPayday; // an estimate; empty for irregular income
        std::optional<Money> remainder; // set when it closed (can be negative)

        [[nodiscard]] bool isOpen() const { return !end.has_value(); }
    };

    struct IncomeLine {
        IncomeId id = 0;
        PeriodId periodId = 0;
        Date date;
        std::string label;
        Money amount = 0;
    };

    struct PeriodSummary {
        Period period;
        Money income = 0;
        Money spentFromEarned = 0; // entries paid from earned, up to today
        Money spentFromSavings = 0;
        std::optional<int> daysLate; // payday has passed and no pay was recorded

        // Earned remaining = income entries minus entries funded from earned (can be negative).
        [[nodiscard]] Money earnedRemaining() const { return income - spentFromEarned; }
    };

    struct PaidOptions {
        Date paidOn; // the day pay arrived (an earlier day is fine)
        std::optional<Money> amount; // what arrived; empty = the expected pay
        std::optional<Date> nextPayday; // empty = same day next month (regular only)
        std::optional<PotId> leftoverPot; // empty = Main savings
        bool automatic = false;
    };

    struct PaidResult {
        Date paidOn;
        Date periodStart; // the period that was closed
        Money pay = 0; // added to the new period
        Money remainder = 0; // income minus earned spending (can be negative)
        Money movedToSavings = 0; // the remainder when it is above 0
        Money shortfall = 0; // how far below 0 it ended (nothing is taken from savings)
        std::string leftoverPot; // the pot's name when something was moved
        std::optional<Date> nextPayday;
    };

    // Created and owned by Pesa. Use it through app.periods().
    class Periods {
    public:
        Periods(detail::Database &db, const Settings &settings) noexcept : db_(db), settings_(settings) {
        }

        // Opens the first period if there is none (returns true if it did). A starting
        // balance becomes the first income line ("Starting balance").
        bool ensureOpen(const Date &today, Money startingBalance = 0);

        void setExpectedPayday(std::optional<Date> payday);

        [[nodiscard]] std::optional<Period> current() const;

        [[nodiscard]] std::vector<Period> history() const; // closed periods, newest first
        [[nodiscard]] PeriodSummary summary(const Date &today) const;

        // Money that arrived outside pay (a gift, a project payment).
        IncomeId addIncome(const Date &date, std::string_view label, Money amount);

        [[nodiscard]] std::vector<IncomeLine> income() const; // the open period's income

        // "I got paid". Closes the period, moves a positive leftover into a pot at once,
        // and opens the next period with the pay as its first income.
        PaidResult markPaid(const PaidOptions &options);

        // On launch: closes every period whose expected payday has passed (regular income
        // with auto-pay on). Each is closed on its expected payday, even if you open the
        // app days later. Nothing happens otherwise.
        std::vector<PaidResult> autoPayIfDue(const Date &today);

        // "Salary delayed": moves the expected payday. Must be after today.
        void delayPayday(const Date &newPayday, const Date &today);

        // The same day next month, based on the payday that was expected.
        [[nodiscard]] Date estimateNextPayday(const Date &paidOn) const;

    private:
        [[nodiscard]] Period requireCurrent() const;

        detail::Database &db_;
        const Settings &settings_;
    };
} // namespace pesa
