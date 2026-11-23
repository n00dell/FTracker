//
// Created by lee on 11/23/26.
//
// FTracker/cli/day_cmd.cpp
#include "cli.h"

#include <iomanip>
#include <iostream>

namespace cli {

    namespace {

        std::string stateText(const pesa::Entry& e) {
            switch (e.kind) {
                case pesa::EntryKind::AutoPosted: return "auto";
                case pesa::EntryKind::Edited:     return "edited (was " + std::to_string(e.originalAmount.value_or(0)) + ")";
                case pesa::EntryKind::Added:      return e.isOneOff() ? "added (one-off)" : "added";
                case pesa::EntryKind::Skipped:    return "skipped (not counted)";
            }
            return "";
        }

        void printDay(const pesa::DayView& v) {
            std::cout << pesa::WeekdaySet{v.date.weekday()}.toString() << ' ' << v.date.toString() << "\n";

            if (v.entries.empty()) {
                std::cout << "  (no entries)\n";
            } else {
                std::cout << std::left << std::setw(5) << "ID" << std::setw(14) << "Category"
                          << std::setw(24) << "Entry" << std::right << std::setw(8) << "Amount"
                          << "  " << std::left << std::setw(9) << "From" << "State\n";
                for (const auto& e : v.entries) {
                    std::cout << std::left << std::setw(5) << e.id << std::setw(14) << e.categoryName
                              << std::setw(24) << e.name << std::right << std::setw(8) << e.amount
                              << "  " << std::left << std::setw(9)
                              << (e.fund == pesa::FundedFrom::Earned ? "earned" : "savings")
                              << stateText(e) << "\n";
                }
            }
            std::cout << "Spent: " << v.total() << "  (earned " << v.earnedTotal
                      << ", savings " << v.savingsTotal << ")\n";
        }

    } // namespace

    int runDay(pesa::Pesa& app, const Args& args) {
        if (args.size() > 2) { printUsage(); return 2; }
        const pesa::Date date = args.size() == 2 ? parseDate(args[1]) : pesa::Date::today();
        printDay(app.days().view(date));
        return 0;
    }

} // namespace cli
