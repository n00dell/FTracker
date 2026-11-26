// FTracker/core/include/pesa/rules.h
#pragma once

#include <pesa/common.h>
#include <pesa/corrections.h>
#include <pesa/date.h>

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace pesa {
    // WHEN a rule fires: certain weekdays ("Mon-Fri"), or one day of every month ("the 5th").
    // Build one with Schedule::weekly(...) or Schedule::monthly(...). A schedule that exists
    // is always valid, so the rest of the code never has to re-check it.
    class Schedule {
    public:
        enum class Kind : int { Weekly = 0, Monthly = 1 };

        Schedule() = default; // every day, weekly: so structs that hold one can be default-built

        [[nodiscard]] static Schedule weekly(WeekdaySet days); // throws Error if no day is chosen
        [[nodiscard]] static Schedule monthly(int dayOfMonth); // 1-31, throws Error otherwise
        // Rebuilds a schedule from what the database stores (bits and a day number).
        [[nodiscard]] static Schedule fromStored(Kind kind, unsigned weekdayBits, int dayOfMonth);

        [[nodiscard]] Kind kind() const { return kind_; }
        [[nodiscard]] WeekdaySet weekdays() const { return days_; } // meaningful for Weekly
        [[nodiscard]] int dayOfMonth() const { return monthDay_; } // meaningful for Monthly

        // Does this schedule fire on that date? Monthly rules set for the 31st fire on the
        // LAST day of shorter months, so they are never silently skipped.
        [[nodiscard]] bool occursOn(const Date &date) const;

        [[nodiscard]] std::string toString() const; // "Mon-Fri", "Mon,Wed", "Monthly, day 5"

    private:
        Kind kind_ = Kind::Weekly;
        WeekdaySet days_ = WeekdaySet::everyDay();
        int monthDay_ = 0;
    };

    // Charge: posts its items on its schedule. Adjust: adds `extra` KES to each posting of
    // its items for the dates it covers ("tea up by 10 for a period, then back to normal").
    enum class RuleKind : int { Charge = 0, Adjust = 1 };

    struct RuleItem {
        ItemId id = 0;
        std::string name;
    };

    struct Rule {
        RuleId id = 0;
        std::string name;
        Schedule schedule; // an adjust rule always has "every day"
        Date effectiveFrom;
        std::optional<Date> effectiveTo; // last day it applies; empty = never ends
        std::vector<RuleItem> items; // the items it covers (deleted items are left out)
        bool isActive = true; // false = paused
        bool isDeleted = false;
        RuleKind kind = RuleKind::Charge;
        Money extra = 0; // adjust rules: KES added to each posting (can be negative)

        // The question the daily job asks: should this rule act on that date?
        [[nodiscard]] bool appliesOn(const Date &date) const;
    };

    struct NewRule {
        std::string name;
        Schedule schedule;
        Date effectiveFrom;
        std::optional<Date> effectiveTo;
        std::vector<ItemId> itemIds;
        RuleKind kind = RuleKind::Charge;
        Money extra = 0;
    };

    // Created and owned by Pesa. Use it through app.rules().
    //
    // Per the spec, rules are not edited in place: to change WHEN something posts, end the
    // old rule and add a new one from a new date, so history stays correct. What you can do
    // is rename, pause, end, delete, and change which items a rule covers.
    class Rules {
    public:
        explicit Rules(detail::Database &db) noexcept : db_(db) {
        }

        // Charge rules only. (An adjust rule can reach back into posted days, so it has its own door.)
        RuleId add(const NewRule &rule);

        // An adjust rule. If it starts in the past it recalculates the entries already posted:
        // always call it with Mode::Preview first and show the person what would change.
        // Entries you edited by hand are skipped. A restore point is saved before any change.
        CorrectionResult addAdjustment(const NewRule &rule, const Date &today, Mode mode = Mode::Apply);

        void rename(RuleId id, std::string_view newName);

        void setActive(RuleId id, bool active);

        void endOn(RuleId id, const Date &lastDay); // the rule stops applying after this day
        void addItem(RuleId id, ItemId item);

        void removeItem(RuleId id, ItemId item);

        void remove(RuleId id); // soft delete: hides it, keeps the row
        void restore(RuleId id); // undo a delete (fails if the name is taken again)

        [[nodiscard]] std::optional<Rule> find(RuleId id) const;

        [[nodiscard]] std::vector<Rule> list(Visibility show = Visibility::ActiveOnly) const;

    private:
        detail::Database &db_;
    };
} // namespace pesa
