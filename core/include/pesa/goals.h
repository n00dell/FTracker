// FTracker/core/include/pesa/goals.h
#pragma once

#include <pesa/common.h>
#include <pesa/date.h>
#include <pesa/entries.h>    // FundedFrom
#include <pesa/settings.h>

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace pesa {

    // How a purchase is paid. The numbers are stored in the file, so never renumber them.
    enum class BuyMethod : int {
        FromSavings  = 0,   // update savings: the price comes out of a pot (Main savings unless you pick)
        MoveToEarned = 1,   // move the price from savings into earned, then it is spent from earned
        FromEarned   = 2,   // spend it from earned
    };

    struct Goal {
        GoalId                 id = 0;
        std::string            name;
        Money                  amount = 0;
        int                    priority = 0;                 // lower comes first: 1 beats 100
        std::optional<Date>    dueDate;
        FundedFrom             fundedFrom = FundedFrom::Earned;
        bool                   donated = false;              // a gift: buying it deducts nothing
        bool                   autoBuy = false;              // buys itself on its date (needs a date)
        std::optional<Date>    boughtOn;                     // empty = still to buy
        std::optional<BuyMethod> boughtHow;                  // empty for a gift

        [[nodiscard]] bool isBought() const { return boughtOn.has_value(); }
    };

    struct NewGoal {
        std::string         name;
        Money               amount = 0;
        int                 priority = 0;
        std::optional<Date> dueDate;
        FundedFrom          fundedFrom = FundedFrom::Earned;
        bool                donated = false;
        bool                autoBuy = false;
    };

    // Only the fields that are set change. To remove the date, set `clearDate`.
    struct GoalEdit {
        std::optional<std::string> name;
        std::optional<Money>       amount;
        std::optional<int>         priority;
        std::optional<Date>        dueDate;
        bool                       clearDate = false;
        std::optional<FundedFrom>  fundedFrom;
        std::optional<bool>        donated;
        std::optional<bool>        autoBuy;
    };

    // How an open goal stands against the money it is funded from. Goals are counted in
    // priority order, so `shortBy` is how much MORE you would need to afford this goal
    // AND every goal above it that is paid from the same place.
    struct GoalStatus {
        Goal  goal;
        bool  covered = false;
        Money shortBy = 0;
    };

    // What happened to goals that reached their date.
    struct GoalsDue {
        std::vector<Goal>        bought;     // bought automatically just now
        std::vector<Goal>        waiting;    // due, and waiting for the person to decide
        std::vector<std::string> problems;
    };

    // A target you save towards, tracked against a pot. It reads the pot's balance,
    // so nothing is stored twice.
    struct Fund {
        FundId      id = 0;
        std::string name;
        Money       target = 0;
        PotId       potId = 0;
        std::string potName;
        Money       balance = 0;
    };

    // Created and owned by Pesa. Use it through app.goals().
    class Goals {
    public:
        Goals(detail::Database& db, const Settings& settings) noexcept : db_(db), settings_(settings) {}

        // ---- purchase goals ----
        GoalId add(const NewGoal& goal);
        void   edit(GoalId id, const GoalEdit& changes);   // a bought goal cannot be changed
        void   remove(GoalId id);                          // soft delete: hides it, frees its name

        [[nodiscard]] std::optional<Goal>  find(GoalId id) const;
        // Goals still to buy, by priority; or, with `bought`, the ones you bought (newest first).
        [[nodiscard]] std::vector<Goal>    list(bool bought = false) const;
        [[nodiscard]] std::vector<GoalStatus> statuses(const Date& today) const;   // open goals, with covered/short

        // Marks a goal bought and moves the money, in one step. A gift moves nothing.
        // `pot` is the pot a savings purchase comes out of (Main savings when empty).
        void buy(GoalId id, BuyMethod method, const Date& date, std::optional<PotId> pot = std::nullopt);

        // On launch: goals whose date has come. Those set to buy themselves are bought;
        // the rest are listed so the person can decide.
        GoalsDue processDue(const Date& today);

        // ---- fund goals ----
        FundId addFund(std::string_view name, Money target, PotId pot);
        void   removeFund(FundId id);
        [[nodiscard]] std::vector<Fund> funds() const;

    private:
        detail::Database& db_;
        const Settings&   settings_;
    };

} // namespace pesa