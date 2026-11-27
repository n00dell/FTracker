// FTracker/core/include/pesa/income.h
#pragma once

#include <pesa/common.h>
#include <pesa/date.h>
#include <pesa/settings.h>

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace pesa {

    // How often other income arrives. The numbers are stored in the file, so never renumber them.
    enum class Frequency : int { OneOff = 0, WithPay = 1, Monthly = 2, Quarterly = 3, Yearly = 4 };

    [[nodiscard]] std::string frequencyName(Frequency frequency);   // "Every 3 months"

    // One row of the pay history: the amount and the days it applied (`to` empty = still applies).
    struct PayRate {
        Money               amount = 0;
        Date                from;
        std::optional<Date> to;
    };

    struct IncomeSource {
        SourceId             id = 0;
        std::string          name;
        Money                usualAmount = 0;
        Frequency            frequency = Frequency::Monthly;
        std::optional<Date>  nextExpected;     // empty = no plan yet (Pesa never asks), or "with my pay"
        std::optional<PotId> potId;            // empty = earned
        std::string          goesTo;           // "Earned" or the pot's name
        std::optional<Date>  snoozedOn;        // "not yet": not asked again today
    };

    struct NewSource {
        std::string          name;
        Money                usualAmount = 0;
        Frequency            frequency = Frequency::Monthly;
        std::optional<Date>  nextExpected;
        std::optional<PotId> potId;
    };

    struct SourceReceived {
        std::string         name;
        Money               amount = 0;
        std::string         goesTo;
        std::optional<Date> nextExpected;      // when it is expected next (recurring income)
        bool                removed = false;   // a one-off is removed once it arrived
    };

    // Other income that was added to the new period together with the pay.
    struct PaidExtra {
        std::string name;
        Money       amount = 0;
        std::string goesTo;
    };

    // Created and owned by Pesa. Use it through app.income().
    //
    // Income that has not arrived yet counts for nothing: it stays out of earned remaining.
    // Unplanned money (a gift) goes through Periods::addIncome instead.
    class Income {
    public:
        Income(detail::Database& db, const Settings& settings) noexcept : db_(db), settings_(settings) {}

        // ---- pay history ----
        // A raise: this amount from this day. The old amount ends the day before; past periods keep
        // what they had. A day that already has a row corrects that row. Earlier days are refused.
        void setPay(Money amount, const Date& from);
        // The pay that applies on a day (what "I got paid" adds).
        [[nodiscard]] std::optional<Money> payOn(const Date& date) const;
        [[nodiscard]] std::vector<PayRate> payHistory() const;   // oldest first

        // ---- other income ----
        SourceId addSource(const NewSource& source);
        void     removeSource(SourceId id);   // soft delete
        [[nodiscard]] std::optional<IncomeSource>  findSource(SourceId id) const;
        [[nodiscard]] std::vector<IncomeSource>    sources() const;   // with a date first, soonest first

        // Sources whose expected date has come and that were not put off today: the questions to ask.
        [[nodiscard]] std::vector<IncomeSource> due(const Date& today) const;

        // "Yes, it arrived": counts the money now (this period's earned, or into its pot). `amount`
        // empty = the usual amount. A one-off is removed; recurring income moves to its next date.
        SourceReceived received(SourceId id, std::optional<Money> amount, const Date& today);
        // "Skipped this time": moves recurring income to its next date without any money.
        Date skip(SourceId id, const Date& today);
        // "Not yet": asked again tomorrow.
        void notYet(SourceId id, const Date& today);

        // Called by "I got paid": adds the "with my pay" income to the new period.
        std::vector<PaidExtra> postWithPay(const Date& date, PeriodId period);

    private:
        detail::Database& db_;
        const Settings&   settings_;
    };

} // namespace pesa