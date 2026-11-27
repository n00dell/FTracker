//
// Created by lee on 11/26/26.
//
// FTracker/cli/goal_cmd.cpp
// Goals (things to buy) and funds (a target you save towards).
#include <cli/commands.h>

#include <cli/display.h>
#include <cli/pickers.h>

namespace cli {

namespace {

// A pot typed as an id or a name. (A third copy of this idea: we will tidy the duplicates in one refactor.)
pesa::PotId potRef(Context& ctx, const std::string& text) {
    if (const auto id = parseInt(text)) return *id;
    for (const auto& pot : ctx.app.savings().pots()) {
        if (toLower(pot.name) == toLower(text)) return pot.id;
    }
    throw pesa::Error("No pot named '" + text + "'.");
}

int priorityOf(const std::string& text) {
    const auto number = parseInt(text);
    if (!number || *number < 1 || *number > 1000000) {
        throw pesa::Error("The priority must be a whole number from 1 up (lower comes first).");
    }
    return static_cast<int>(*number);
}

// The goal id typed, or a numbered list to pick from.
pesa::GoalId goalIdArg(Context& ctx, ArgReader& reader, const std::string& label) {
    if (const auto text = reader.next()) return parseId(*text, "goal");
    const auto goals = ctx.app.goals().list(false);
    if (goals.empty()) throw pesa::Error("There are no goals to pick from. Try: goal add");
    std::vector<std::string> options;
    for (const auto& g : goals) options.push_back(g.name + "  " + formatMoney(g.amount));
    return goals[ctx.io.askChoice(label, options)].id;
}

} // namespace

int runGoal(Context& ctx, const Args& args) {
    auto& io = ctx.io;
    auto& goals = ctx.app.goals();
    const pesa::Date today = pesa::Date::today();

    Args rest = args;
    const bool savingsFlag = takeFlag(rest, "--savings");
    const bool earnedFlag  = takeFlag(rest, "--earned");
    const bool gift        = takeFlag(rest, "--gift");
    const bool noGift      = takeFlag(rest, "--nogift");
    const bool autoBuy     = takeFlag(rest, "--auto");
    const bool noAuto      = takeFlag(rest, "--noauto");
    const bool noDate      = takeFlag(rest, "--nodate");
    const bool boughtList  = takeFlag(rest, "--bought");
    const auto priorityText = takeOption(rest, "--priority");
    const auto dateText     = takeOption(rest, "--date");
    const auto nameText     = takeOption(rest, "--name");
    const auto amountText   = takeOption(rest, "--amount");
    const auto howText      = takeOption(rest, "--how");
    const auto potText      = takeOption(rest, "--pot");
    if (savingsFlag && earnedFlag) throw pesa::Error("Use either --savings or --earned, not both.");
    if (gift && noGift) throw pesa::Error("Use either --gift or --nogift, not both.");
    if (autoBuy && noAuto) throw pesa::Error("Use either --auto or --noauto, not both.");

    const std::string sub = rest.size() >= 2
        ? rest[1]
        : chooseSubcommand(io, "Goals", {{"Add a goal", "add"}, {"List goals", "list"}, {"Change a goal", "edit"},
                                         {"Buy a goal", "buy"}, {"Delete a goal", "delete"}, {"What is due", "due"}});
    ArgReader reader(rest, 2);

    if (sub == "list") {
        if (boughtList) printBoughtGoals(io, goals.list(true));
        else            printGoals(io, goals.statuses(today));
        return 0;
    }

    if (sub == "due") {
        std::vector<pesa::GoalStatus> due;
        for (auto& status : goals.statuses(today)) {
            if (status.goal.dueDate && *status.goal.dueDate <= today) due.push_back(std::move(status));
        }
        if (due.empty()) io.out() << "  Nothing is due.\n"; else printGoals(io, due);
        return 0;
    }

    if (sub == "add") {
        const auto priceWord = reader.next();
        const auto typed = reader.rest();
        const pesa::Money price = priceWord ? parseAmount(*priceWord) : io.askAmount("Price (KES)");
        const std::string name = typed ? *typed : io.askText("Goal name");

        int priority = 0;
        if (priorityText) {
            priority = priorityOf(*priorityText);
        } else {
            const auto open = goals.list(false);           // by priority: the last one is the lowest priority
            priority = open.empty() ? 10 : open.back().priority + 10;
        }

        pesa::NewGoal goal{.name = name, .amount = price, .priority = priority,
                           .fundedFrom = savingsFlag ? pesa::FundedFrom::Savings : pesa::FundedFrom::Earned,
                           .donated = gift, .autoBuy = autoBuy};
        if (dateText) goal.dueDate = parseDate(*dateText);

        const auto id = goals.add(goal);
        io.out() << "Added goal #" << id << " (priority " << priority << ").\n\n";
        printGoals(io, goals.statuses(today));
        return 0;
    }

    if (sub == "edit") {
        const auto id = goalIdArg(ctx, reader, "Change which goal");
        pesa::GoalEdit edit;
        bool changed = false;
        if (nameText)     { edit.name = *nameText;                  changed = true; }
        if (amountText)   { edit.amount = parseAmount(*amountText); changed = true; }
        if (priorityText) { edit.priority = priorityOf(*priorityText); changed = true; }
        if (dateText)     { edit.dueDate = parseDate(*dateText);    changed = true; }
        if (noDate)       { edit.clearDate = true;                  changed = true; }
        if (savingsFlag)  { edit.fundedFrom = pesa::FundedFrom::Savings; changed = true; }
        if (earnedFlag)   { edit.fundedFrom = pesa::FundedFrom::Earned;  changed = true; }
        if (gift)         { edit.donated = true;                    changed = true; }
        if (noGift)       { edit.donated = false;                   changed = true; }
        if (autoBuy)      { edit.autoBuy = true;                    changed = true; }
        if (noAuto)       { edit.autoBuy = false;                   changed = true; }
        if (!changed) throw pesa::Error("Say what to change, for example --amount 12,000 or --date 2026-12-20.");

        goals.edit(id, edit);
        io.out() << "Updated.\n\n";
        printGoals(io, goals.statuses(today));
        return 0;
    }

    if (sub == "buy") {
        const auto id = goalIdArg(ctx, reader, "Buy which goal");
        const auto goal = goals.find(id);
        if (!goal) throw pesa::Error("No goal with id " + std::to_string(id) + ".");

        std::optional<pesa::PotId> pot;
        if (potText) pot = potRef(ctx, *potText);

        pesa::BuyMethod method = goal->fundedFrom == pesa::FundedFrom::Savings ? pesa::BuyMethod::FromSavings
                                                                               : pesa::BuyMethod::FromEarned;
        if (goal->donated) {
            io.out() << "'" << goal->name << "' is a gift, so nothing is deducted.\n";
        } else if (howText) {
            const std::string how = toLower(*howText);
            if      (how == "savings") method = pesa::BuyMethod::FromSavings;
            else if (how == "move")    method = pesa::BuyMethod::MoveToEarned;
            else if (how == "earned")  method = pesa::BuyMethod::FromEarned;
            else throw pesa::Error("Use --how savings, move or earned.");
        } else {
            const auto choice = io.askChoice("How do you pay for it",
                {"Update savings (take it from savings)", "Move savings to earned, then cut from earned", "Cut from earned"},
                method == pesa::BuyMethod::FromSavings ? 0 : 2);
            method = choice == 0 ? pesa::BuyMethod::FromSavings
                   : choice == 1 ? pesa::BuyMethod::MoveToEarned : pesa::BuyMethod::FromEarned;
        }

        if (!goal->donated && method != pesa::BuyMethod::FromEarned) {   // warn first; nothing is blocked
            const auto info = ctx.app.savings().findPot(pot.value_or(ctx.app.savings().mainPot()));
            if (info && info->kind == pesa::PotKind::Locked) {
                io.out() << "Note: '" << info->name << "' is a locked pot. Taking money out early may cost you.\n";
            }
            if (info && info->balance < goal->amount) {
                io.out() << "Note: '" << info->name << "' only has " << formatMoney(info->balance) << ".\n";
            }
        }
        goals.buy(id, method, today, pot);
        io.out() << "Marked '" << goal->name << "' as bought.\n";
        autoCoverAfterChange(ctx);
        return 0;
    }

    if (sub == "delete") {
        const auto id = goalIdArg(ctx, reader, "Delete which goal");
        goals.remove(id);
        io.out() << "Deleted.\n";
        return 0;
    }

    printUsage(io);
    return 2;
}

int runFund(Context& ctx, const Args& args) {
    auto& io = ctx.io;
    auto& goals = ctx.app.goals();

    Args rest = args;
    const auto potText = takeOption(rest, "--pot");
    const std::string sub = rest.size() >= 2
        ? rest[1]
        : chooseSubcommand(io, "Funds", {{"Add a fund", "add"}, {"List funds", "list"}, {"Delete a fund", "delete"}});
    ArgReader reader(rest, 2);

    if (sub == "list") {
        printFunds(io, goals.funds());
        return 0;
    }
    if (sub == "add") {
        const auto targetWord = reader.next();
        const auto typed = reader.rest();
        const pesa::Money target = targetWord ? parseAmount(*targetWord) : io.askAmount("Target (KES)");
        const std::string name = typed ? *typed : io.askText("Fund name (e.g. Emergency fund)");
        const pesa::PotId pot = potText ? potRef(ctx, *potText) : ctx.app.savings().mainPot();
        const auto id = goals.addFund(name, target, pot);
        io.out() << "Added fund #" << id << ".\n\n";
        printFunds(io, goals.funds());
        return 0;
    }
    if (sub == "delete") {
        pesa::FundId id = 0;
        if (const auto text = reader.next()) {
            id = parseId(*text, "fund");
        } else {
            const auto funds = goals.funds();
            if (funds.empty()) throw pesa::Error("There are no funds to delete.");
            std::vector<std::string> options;
            for (const auto& f : funds) options.push_back(f.name);
            id = funds[io.askChoice("Delete which fund", options)].id;
        }
        goals.removeFund(id);
        io.out() << "Deleted.\n";
        return 0;
    }
    printUsage(io);
    return 2;
}

} // namespace cli