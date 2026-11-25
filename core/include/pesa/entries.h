// FTracker/core/include/pesa/entries.h
#pragma once

#include <pesa/common.h>
#include <pesa/date.h>
#include <span>
#include <optional>
#include <string>
#include <string_view>

namespace pesa {

enum class EntryKind : int {
    AutoPosted = 0,   // posted by the daily job, untouched
    Edited     = 1,   // an auto entry you changed (bulk changes skip it)
    Added      = 2,   // you added it by hand
    Skipped    = 3,   // a skip rule removed it for that day: shown, but not counted
};

enum class FundedFrom : int { Earned = 0, Savings = 1 };

struct Entry {
    EntryId       id = 0;
    Date          date;
    std::optional<ItemId>     itemId;       // empty = a one-off (not from the catalog)
    std::optional<CategoryId> categoryId;   // empty = a one-off: it belongs to no category
    std::string   categoryName;             // current name of the category; empty for a one-off
    std::string   name;                     // saved with the entry, so history never changes
    Money         amount = 0;
    std::optional<Money> originalAmount;    // what was posted, if you changed an auto entry
    EntryKind     kind = EntryKind::Added;
    FundedFrom    fund = FundedFrom::Earned;
    std::optional<RuleId> ruleId;           // the rule that posted it (auto entries only)

    [[nodiscard]] bool isOneOff() const { return !itemId.has_value(); }
};

// Created and owned by Pesa. Use it through app.entries().
// Every change happens inside one transaction together with the day's recalculation,
// so a day is never left half-written.
class Entries {
public:
    explicit Entries(detail::Database& db) noexcept : db_(db) {}

    // Pick an item from the catalog. Name and category are copied from it; the amount
    // defaults to the item's default amount. Future and past dates are both allowed.
    EntryId addFromItem(const Date& date, ItemId item,
                        std::optional<Money> amount = std::nullopt,
                        FundedFrom fund = FundedFrom::Earned);

    // A single-day entry that is NOT in the catalog and has no category (Uncle Bill's
    // wedding). The dashboard shows it in its own "One-offs" bucket.
    EntryId addOneOff(const Date& date, std::string_view name, Money amount,
                      FundedFrom fund = FundedFrom::Earned);

    // Changing an auto entry's amount makes it "edited" and remembers the original.
    void setAmount(EntryId id, Money amount);
    void setFundedFrom(EntryId id, FundedFrom fund);
    void remove(EntryId id);
    // "Cover from savings" for several entries at once: they switch to funded from savings,
    // and each gets its ledger line. Already-covered entries are skipped. Returns the total
    // newly covered. A pot with too little is allowed to go below 0; the caller warns first.
    Money coverFromSavings(std::span<const EntryId> ids);

    [[nodiscard]] std::optional<Entry> find(EntryId id) const;

private:
    detail::Database& db_;
};

} // namespace pesa