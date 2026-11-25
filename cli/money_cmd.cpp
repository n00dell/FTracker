//
// Created by lee on 11/25/26.
//
// FTracker/cli/money_cmd.cpp
// Periods, income and savings: status, paid, delay, income, savings.
#include <cli/commands.h>
#include <cli/pickers.h>
#include <cli/display.h>

#include <algorithm>

namespace cli {

namespace {

// A pot typed as an id or a name.
pesa::PotId potRef(Context& ctx, const std::string& text) {
    if (const auto id = parseInt(text)) return *id;
    for (const auto& pot : ctx.app.savings().pots()) {
        if (toLower(pot.name) == toLower(text)) return pot.id;
    }
    throw pesa::Error("No pot named '" + text + "'.");
}

// --pot <name|id>, or Main savings.
pesa::PotId potOption(Context& ctx, Args& args) {
    if (const auto text = takeOption(args, "--pot")) return potRef(ctx, *text);
    return ctx.app.savings().mainPot();
}

} // namespace

int runStatus(Context& ctx, const Args& args) {
    if (args.size() > 1) { printUsage(ctx.io); return 2; }
    printStatus(ctx.io.out(), ctx.app.periods().summary(pesa::Date::today()), ctx.app.savings().total(),
                ctx.app.settings().incomeMode());
    if (const int issues = ctx.app.log().issueCount(); issues > 0) {
        ctx.io.out() << "  " << issues << (issues == 1 ? " issue" : " issues") << " in the log. See 'log issues'.\n";
    }
    return 0;
}

int runPaid(Context& ctx, const Args& args) {
    auto& io = ctx.io;
    auto& periods = ctx.app.periods();

    Args rest = args;
    const auto dateText   = takeOption(rest, "--date");
    const auto amountText = takeOption(rest, "--amount");
    const auto nextText   = takeOption(rest, "--next");
    const auto potText    = takeOption(rest, "--pot");
    if (rest.size() > 1) { printUsage(io); return 2; }
    const bool guided = !dateText && !amountText && !nextText && !potText;   // a bare 'paid' asks

    const pesa::Date today = pesa::Date::today();
    const bool regular = ctx.app.settings().incomeMode() == pesa::IncomeMode::Regular;

    pesa::PaidOptions options;
    options.paidOn = dateText ? parseDate(*dateText) : (guided ? io.askDate("Date you were paid", today) : today);

    if (amountText) options.amount = parseAmount(*amountText);
    else if (guided) options.amount = io.askAmount("Amount that arrived (KES)", ctx.app.settings().expectedPay());

    if (nextText) options.nextPayday = parseDate(*nextText);
    else if (guided && regular) {
        options.nextPayday = io.askDate("Next payday (an estimate)", periods.estimateNextPayday(options.paidOn));
    }
    if (potText) options.leftoverPot = potRef(ctx, *potText);

    printPaid(io.out(), periods.markPaid(options));
    return 0;
}

int runDelay(Context& ctx, const Args& args) {
    auto& io = ctx.io;
    auto& periods = ctx.app.periods();
    const pesa::Date today = pesa::Date::today();

    const auto expected = periods.summary(today).period.expectedPayday;
    if (!expected) throw pesa::Error("There is no expected payday to move.");

    const std::string text = args.size() >= 2 ? args[1]
                                              : io.askText("Delay by how many days (or give a date)", std::string("3"));
    pesa::Date target;
    if (const auto days = parseInt(text)) {
        if (*days < 1) throw pesa::Error("Delay by at least 1 day.");
        // From the expected payday, or from today when that has already passed.
        target = std::max(*expected, today).addDays(static_cast<int>(*days));
    } else {
        target = parseDate(text);
    }
    periods.delayPayday(target, today);
    io.out() << "Expected payday is now " << target.toString() << ".\n";
    return 0;
}

int runIncome(Context& ctx, const Args& args) {
    auto& io = ctx.io;
    auto& periods = ctx.app.periods();

    const std::string sub = args.size() >= 2
        ? args[1]
        : chooseSubcommand(io, "Income", {{"Add income", "add"}, {"List this period's income", "list"}});
    ArgReader reader(args, 2);

    if (sub == "add") {
        const auto amountText = reader.next();
        const auto labelText = reader.rest();
        const pesa::Money amount = amountText ? parseAmount(*amountText) : io.askAmount("Amount (KES)");
        const std::string label = labelText ? *labelText : io.askText("Who is it from");
        periods.addIncome(pesa::Date::today(), label, amount);
        io.out() << "Added.\n\n";
        printIncome(io, periods.income());
        return 0;
    }
    if (sub == "list") {
        printIncome(io, periods.income());
        return 0;
    }
    printUsage(io);
    return 2;
}

int runSavings(Context& ctx, const Args& args) {
    auto& io = ctx.io;
    auto& savings = ctx.app.savings();
    const pesa::Date today = pesa::Date::today();

    Args rest = args;
    const bool locked = takeFlag(rest, "--locked");
    const bool tied = takeFlag(rest, "--tied");
    const std::string sub = rest.size() >= 2 ? rest[1] : "show";

    if (sub == "show") {
        printPots(io, savings.pots());
        return 0;
    }
    if (sub == "ledger") {
        printLedger(io, savings.ledger());
        return 0;
    }
    if (sub == "newpot") {
        ArgReader reader(rest, 2);
        const auto typed = reader.rest();
        const auto id = savings.addPot(typed ? *typed : io.askText("Pot name"),
                                       locked ? pesa::PotKind::Locked : tied ? pesa::PotKind::Tied : pesa::PotKind::Open);
        io.out() << "Added pot #" << id << ".\n\n";
        printPots(io, savings.pots());
        return 0;
    }
    if (sub == "add" || sub == "move") {
        const auto pot = potOption(ctx, rest);          // takes --pot out of the words first
        ArgReader reader(rest, 2);
        const auto amountText = reader.next();
        const auto note = reader.rest();
        const pesa::Money amount = amountText ? parseAmount(*amountText) : io.askAmount("Amount (KES)");

        if (sub == "add") {
            savings.add(today, pot, amount, note ? *note : "Added by hand");
            io.out() << "Added.\n\n";
        } else {
            const auto info = savings.findPot(pot);
            if (info && info->kind == pesa::PotKind::Locked) {
                io.out() << "Note: '" << info->name << "' is a locked pot. Taking money out early may cost you.\n";
            }
            if (info && amount > info->balance) {
                io.out() << "Note: '" << info->name << "' only has " << formatMoney(info->balance) << ".\n";
            }
            savings.moveToEarned(today, pot, amount);
            io.out() << "Moved " << formatMoney(amount) << " to earned.\n\n";
        }
        printPots(io, savings.pots());
        return 0;
    }
    if (sub == "cover") {
        ArgReader reader(rest, 2);
        std::vector<pesa::EntryId> ids;
        while (const auto text = reader.next()) ids.push_back(parseId(*text, "entry"));
        if (ids.empty()) throw pesa::Error("Give at least one entry id (see 'day' for the ids).");

        const pesa::Money total = ctx.app.entries().coverFromSavings(ids);
        io.out() << "Covered " << formatMoney(total) << " from Main savings.\n";
        if (const auto main = savings.findPot(savings.mainPot()); main && main->balance < 0) {
            io.out() << "Note: Main savings is now below zero (" << formatMoney(main->balance) << ").\n";
        }
        return 0;
    }
    printUsage(io);
    return 2;
}

} // namespace cli