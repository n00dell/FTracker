//
// Created by lee on 11/23/26.
//
// FTracker/cli/entry_cmd.cpp
#include "cli.h"

#include <iostream>

namespace cli {

int runEntry(pesa::Pesa& app, const Args& args) {
    Args rest = args;
    const bool savings = takeFlag(rest, "--savings");
    const auto fund = savings ? pesa::FundedFrom::Savings : pesa::FundedFrom::Earned;
    if (rest.size() < 2) { printUsage(); return 2; }
    const std::string& sub = rest[1];

    // entry add <date> <item-id> [<amount>]
    if (sub == "add" && (rest.size() == 4 || rest.size() == 5)) {
        const auto itemId = parseInt(rest[3]);
        if (!itemId) { std::cerr << "Not a valid item id: " << rest[3] << "\n"; return 2; }
        std::optional<pesa::Money> amount;
        if (rest.size() == 5) amount = parseAmount(rest[4]);

        const auto id = app.entries().addFromItem(parseDate(rest[2]), *itemId, amount, fund);
        std::cout << "Added entry #" << id << "\n";
        return 0;
    }

    // entry oneoff <date> <category> <amount> <name...>
    if (sub == "oneoff" && rest.size() >= 6) {
        const auto id = app.entries().addOneOff(parseDate(rest[2]), resolveCategory(app, rest[3]),
                                                joinFrom(rest, 5), parseAmount(rest[4]), fund);
        std::cout << "Added one-off entry #" << id << "\n";
        return 0;
    }

    if (rest.size() >= 3) {
        const auto id = parseInt(rest[2]);
        if (!id) { std::cerr << "Not a valid id: " << rest[2] << "\n"; return 2; }

        if (sub == "amount" && rest.size() == 4) {
            app.entries().setAmount(*id, parseAmount(rest[3]));
            std::cout << "Amount updated.\n";
            return 0;
        }
        if (sub == "fund" && rest.size() == 4) {
            if (rest[3] != "earned" && rest[3] != "savings") {
                std::cerr << "Use 'earned' or 'savings'.\n";
                return 2;
            }
            app.entries().setFundedFrom(*id, rest[3] == "savings" ? pesa::FundedFrom::Savings
                                                                  : pesa::FundedFrom::Earned);
            std::cout << "Updated.\n";
            return 0;
        }
        if (sub == "delete" && rest.size() == 3) {
            app.entries().remove(*id);
            std::cout << "Deleted.\n";
            return 0;
        }
    }
    printUsage();
    return 2;
}

} // namespace cli
