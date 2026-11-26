// FTracker/core/include/pesa/savings.h
#pragma once

#include <pesa/common.h>
#include <pesa/date.h>

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace pesa {
    // Open = spendable. Locked = withdrawing early is allowed, but the UI should warn.
    // Tied = contributions only (like a Sacco).
    enum class PotKind : int { Open = 0, Locked = 1, Tied = 2 };

    // Why money moved. The numbers are stored in the file, so never renumber them.
    enum class LedgerKind : int {
        OpeningBalance = 0,
        PeriodRemainder = 1, // the leftover of a closed period
        AddedToSavings = 2,
        MovedToEarned = 3,
        ExpenseCover = 4, // an entry paid from savings
    };

    // Named "ledgerKindName", not "toString": doctest calls toString() on values it prints,
    // and a same-named function in our namespace would hijack that call.
    [[nodiscard]] std::string ledgerKindName(LedgerKind kind); // "Period remainder"

    struct Pot {
        PotId id = 0;
        std::string name;
        PotKind kind = PotKind::Open;
        bool isMain = false; // "Main savings": built in, the pot that covers expenses
        Money balance = 0; // the sum of its ledger lines (can go below 0: nothing is blocked)
    };

    struct LedgerLine {
        LedgerId id = 0;
        Date date;
        PotId potId = 0;
        std::string potName;
        Money amount = 0; // + into the pot, - out of it
        LedgerKind kind = LedgerKind::AddedToSavings;
        std::string note;
        std::optional<EntryId> entryId; // the entry it pays for, if any
    };

    // Created and owned by Pesa. Use it through app.savings().
    // Savings is stored, not cached: every balance is the sum of ledger lines.
    class Savings {
    public:
        explicit Savings(detail::Database &db) noexcept : db_(db) {
        }

        PotId addPot(std::string_view name, PotKind kind = PotKind::Open);

        [[nodiscard]] PotId mainPot() const;

        [[nodiscard]] std::optional<Pot> findPot(PotId id) const;

        [[nodiscard]] std::vector<Pot> pots() const; // Main first, with balances
        [[nodiscard]] Money total() const; // all pots together
        [[nodiscard]] std::vector<LedgerLine> ledger() const; // oldest first

        // Money you set aside (a gift, a bonus).
        void add(const Date &date, PotId pot, Money amount, std::string_view note,
                 LedgerKind kind = LedgerKind::AddedToSavings);

        // Takes money out of a pot and adds it to THIS period's income, so daily spending
        // keeps cutting from earned. A pot with too little is allowed to go below 0; the
        // caller should warn first (and warn about a locked pot).
        void moveToEarned(const Date &date, PotId pot, Money amount);

    private:
        detail::Database &db_;
    };
} // namespace pesa
