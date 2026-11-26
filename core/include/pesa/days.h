// FTracker/core/include/pesa/days.h
#pragma once

#include <pesa/common.h>
#include <pesa/date.h>
#include <pesa/entries.h>
#include <pesa/corrections.h>
#include <optional>
#include <vector>

namespace pesa {
    // Everything the day view needs about one date.
    struct DayView {
        Date date;
        std::vector<Entry> entries; // grouped by category order; skipped ones included
        Money earnedTotal = 0; // skipped entries are not counted
        Money savingsTotal = 0;
        bool calculated = false; // set by the daily job (later)

        [[nodiscard]] Money total() const { return earnedTotal + savingsTotal; }
    };

    // Created and owned by Pesa. Use it through app.days().
    class Days {
    public:
        explicit Days(detail::Database &db) noexcept : db_(db) {
        }

        // Works for any date, even one with no entries (it returns an empty day).
        [[nodiscard]] DayView view(const Date &date) const;

        // Rebuilds the day's saved total from its entries. Entries do this on every change;
        // this is the repair tool for when something went wrong.
        void recalculate(const Date &date);

        // The saved total (the cache the daily job and reports rely on); empty if unknown.
        [[nodiscard]] std::optional<Money> savedResult(const Date &date) const;

        // "Wipe all": removes the entries rules posted that you never touched, for past days in a
        // range (a week off where you spent nothing). Entries you edited or added stay. The days
        // stay marked as posted, so the daily job does not post them again. Call it with
        // Mode::Preview first. The last day cannot be after today.
        CorrectionResult wipe(const Date &from, const Date &to, const Date &today, Mode mode = Mode::Apply);

    private:
        detail::Database &db_;
    };
} // namespace pesa
