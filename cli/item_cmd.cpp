// FTracker/cli/item_cmd.cpp
#include "cli.h"

#include <algorithm>
#include <cctype>
#include <iomanip>
#include <iostream>

namespace cli {

namespace {

void printItems(const std::vector<pesa::Item>& items) {
    if (items.empty()) { std::cout << "No items.\n"; return; }
    std::cout << std::left << std::setw(5) << "ID" << std::setw(16) << "Category"
              << std::setw(26) << "Item" << std::right << std::setw(8) << "Amount"
              << "  " << std::left << "Days\n";
    for (const auto& i : items) {
        std::cout << std::left << std::setw(5) << i.id << std::setw(16) << i.categoryName
                  << std::setw(26) << i.name << std::right << std::setw(8) << i.defaultAmount
                  << "  " << std::left << i.days.toString()
                  << (i.isDeleted ? "  (deleted)" : (i.isActive ? "" : "  (inactive)")) << "\n";
    }
}

} // namespace

int runItem(pesa::Pesa& app, const Args& args) {
    if (args.size() < 2) { printUsage(); return 2; }
    const std::string& sub = args[1];

    if (sub == "add") {
        Args rest = args;
        const auto days = takeOption(rest, "--days");
        if (rest.size() < 5) { printUsage(); return 2; }   // item add <category> <amount> <name...>

        const pesa::NewItem item{
            .categoryId    = resolveCategory(app, rest[2]),
            .name          = joinFrom(rest, 4),
            .defaultAmount = parseAmount(rest[3]),
            .days          = days ? pesa::WeekdaySet::parse(*days) : pesa::WeekdaySet::everyDay(),
        };
        const auto id = app.items().add(item);   // add first: if it throws, nothing is printed
        std::cout << "Added item #" << id << "\n";
        return 0;
    }

    if (sub == "list") {
        Args rest = args;
        const auto show = takeVisibility(rest);
        std::optional<pesa::CategoryId> category;
        if (rest.size() >= 3) category = resolveCategory(app, joinFrom(rest, 2));
        printItems(app.items().list(category, show));
        return 0;
    }

    if (args.size() >= 3) {
        const auto id = parseInt(args[2]);
        if (!id) { std::cerr << "Not a valid id: " << args[2] << "\n"; return 2; }

        if (sub == "rename" && args.size() >= 4) {
            app.items().rename(*id, joinFrom(args, 3));
            std::cout << "Renamed.\n";
            return 0;
        }
        if (sub == "amount" && args.size() == 4) {
            app.items().setDefaultAmount(*id, parseAmount(args[3]));
            std::cout << "Amount updated.\n";
            return 0;
        }
        if (sub == "days" && args.size() >= 4) {
            app.items().setDays(*id, pesa::WeekdaySet::parse(joinFrom(args, 3)));
            std::cout << "Days updated.\n";
            return 0;
        }
        if (sub == "deactivate") { app.items().setActive(*id, false); std::cout << "Deactivated.\n"; return 0; }
        if (sub == "activate")   { app.items().setActive(*id, true);  std::cout << "Activated.\n";   return 0; }
        if (sub == "delete")     { app.items().remove(*id);           std::cout << "Deleted.\n";     return 0; }
        if (sub == "restore")    { app.items().restore(*id);          std::cout << "Restored.\n";    return 0; }
    }
    printUsage();
    return 2;
}

} // namespace cli
