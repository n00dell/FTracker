//
// Created by lee on 11/27/26.
//
// FTracker/cli/income_cmd.cpp
// Income: what arrived, the pay history (raises), and other income you expect.
#include <cli/commands.h>

#include <cli/display.h>

#include "cli/pickers.h"

namespace cli {

namespace {

pesa::PotId potRef(Context& ctx, const std::string& text) {
    if (const auto id = parseInt(text)) return *id;
    for (const auto& pot : ctx.app.savings().pots()) {
        if (toLower(pot.name) == toLower(text)) return pot.id;
    }
    throw pesa::Error("No pot named '" + text + "'.");
}

std::optional<pesa::Frequency> frequencyOf(const std::string& text) {
    const std::string t = toLower(text);
    if (t == "once" || t == "oneoff" || t == "one-off") return pesa::Frequency::OneOff;
    if (t == "pay" || t == "withpay")                   return pesa::Frequency::WithPay;
    if (t == "monthly")                                 return pesa::Frequency::Monthly;
    if (t == "3months" || t == "quarterly")             return pesa::Frequency::Quarterly;
    if (t == "yearly")                                  return pesa::Frequency::Yearly;
    return std::nullopt;
}

void tellReceived(Console& io, const pesa::SourceReceived& r) {
    io.out() << r.name << ": KES " << formatMoney(r.amount) << " counted"
             << (r.goesTo == "Earned" ? " in this period's earned" : ", into " + r.goesTo) << ".\n";
    if (r.removed) io.out() << "  It was a one-off, so it is removed.\n";
    else if (r.nextExpected) io.out() << "  Next expected around " << r.nextExpected->toString() << ".\n";
}

} // namespace

// The expected-payment questions: yes (with the amount), skipped this time, not yet, or remove.
// In a script it only reminds you, because a script must never stop and ask.
void answerDueIncome(Context& ctx, bool interactive) {
    auto& io = ctx.io;
    auto& income = ctx.app.income();
    const pesa::Date today = pesa::Date::today();

    for (const pesa::IncomeSource& source : income.due(today)) {
        const std::string what = source.name + " (KES " + formatMoney(source.usualAmount) + ", expected " +
                                 source.nextExpected->toString() + ", " + source.goesTo + ")";
        if (!interactive) {
            io.err() << "Expected income is due: " << what << ". 'income due' to answer.\n";
            continue;
        }
        try {
            std::vector<std::pair<std::string, std::string>> options{{"Yes, it arrived", "yes"}};
            if (source.frequency != pesa::Frequency::OneOff) options.push_back({"Skipped this time", "skip"});
            options.push_back({"Not yet (ask again tomorrow)", "later"});
            options.push_back({"Remove it", "remove"});

            const std::string answer = chooseSubcommand(io, "Did it arrive? " + what, options);
            if (answer == "yes") {
                const pesa::Money amount = io.askAmount("Amount that arrived (KES)", source.usualAmount);
                tellReceived(io, income.received(source.id, amount, today));
            } else if (answer == "skip") {
                io.out() << "Skipped. Next expected around " << income.skip(source.id, today).toString() << ".\n";
            } else if (answer == "later") {
                income.notYet(source.id, today);
                io.out() << "OK, I will ask again tomorrow.\n";
            } else {
                income.removeSource(source.id);
                io.out() << "Removed " << source.name << ".\n";
            }
        } catch (const Cancelled&) {
            io.out() << "Left for now. It will ask again next time.\n";
            return;
        } catch (const GoBack&) {
            io.out() << "Left for now. It will ask again next time.\n";
            return;
        }
    }
}

int runIncome(Context& ctx, const Args& args) {
    auto& io = ctx.io;
    auto& periods = ctx.app.periods();
    auto& income = ctx.app.income();
    const pesa::Date today = pesa::Date::today();

    Args rest = args;
    const auto oftenText = takeOption(rest, "--often");
    const auto nextText  = takeOption(rest, "--next");
    const auto potText   = takeOption(rest, "--pot");
    const auto fromText  = takeOption(rest, "--from");

    const std::string sub = rest.size() >= 2
        ? rest[1]
        : chooseSubcommand(io, "Income", {{"Add income that just arrived", "add"}, {"List this period's income", "list"},
                                          {"Answer what is due", "due"}, {"Expected income", "source"},
                                          {"Pay and raises", "pay"}});
    ArgReader reader(rest, 2);

    if (sub == "add") {
        const auto amountText = reader.next();
        const auto labelText = reader.rest();
        const pesa::Money amount = amountText ? parseAmount(*amountText) : io.askAmount("Amount (KES)");
        const std::string label = labelText ? *labelText : io.askText("Who is it from");
        periods.addIncome(today, label, amount);
        io.out() << "Added.\n\n";
        printIncome(io, periods.income());
        return 0;
    }
    if (sub == "list") {
        printIncome(io, periods.income());
        return 0;
    }

    if (sub == "pay") {   // no amount: the history. With an amount: a raise from a date (today if none)
        const auto amountText = reader.next();
        if (amountText) {
            income.setPay(parseAmount(*amountText), fromText ? parseDate(*fromText) : today);
            io.out() << "Saved. Past periods keep what they had.\n\n";
        }
        printPayHistory(io, income.payHistory());
        return 0;
    }

    if (sub == "due") {
        answerDueIncome(ctx, true);
        return 0;
    }
    if (sub == "got" || sub == "skip" || sub == "later") {
        const auto id = parseId(reader.next().value_or(""), "expected income");
        if (sub == "got") {
            const auto amountText = reader.next();
            tellReceived(io, income.received(id, amountText ? std::optional(parseAmount(*amountText)) : std::nullopt, today));
        } else if (sub == "skip") {
            io.out() << "Skipped. Next expected around " << income.skip(id, today).toString() << ".\n";
        } else {
            income.notYet(id, today);
            io.out() << "OK, it will ask again tomorrow.\n";
        }
        return 0;
    }

    if (sub == "source") {
        const std::string what = rest.size() >= 3
            ? rest[2]
            : chooseSubcommand(io, "Expected income", {{"Add expected income", "add"}, {"List it", "list"}, {"Remove one", "remove"}});
        ArgReader more(rest, 3);

        if (what == "list") {
            printSources(io, income.sources(), today);
            return 0;
        }
        if (what == "remove") {
            income.removeSource(parseId(more.next().value_or(""), "expected income"));
            io.out() << "Removed.\n";
            return 0;
        }
        if (what == "add") {
            const auto amountWord = more.next();
            const auto typed = more.rest();

            pesa::NewSource source;
            source.usualAmount = amountWord ? parseAmount(*amountWord) : 0;
            if (typed) source.name = *typed;

            std::optional<pesa::Frequency> frequency;
            if (oftenText) {
                frequency = frequencyOf(*oftenText);
                if (!frequency) throw pesa::Error("Use --often once, pay, monthly, 3months or yearly.");
            }
            std::vector<Step> steps;
            if (!typed)      steps.push_back({[&] { source.name = io.askText("Who is it from", source.name.empty() ? std::nullopt : std::optional(source.name)); }});
            if (!amountWord) steps.push_back({[&] { source.usualAmount = io.askAmount("Usual amount (KES)", source.usualAmount > 0 ? std::optional(source.usualAmount) : std::nullopt); }});
            if (!frequency) {
                steps.push_back({[&] {
                    const auto pick = io.askChoice("How often", {"One-off", "With my pay", "Monthly", "Every 3 months", "Yearly"}, 2);
                    frequency = static_cast<pesa::Frequency>(pick);
                }});
            }
            if (!nextText) {
                steps.push_back({[&] { source.nextExpected = io.askOptionalDate("Next expected date"); },
                                 [&] { return frequency != pesa::Frequency::WithPay; }});
            }
            runSteps(steps);

            source.frequency = *frequency;
            if (nextText && source.frequency != pesa::Frequency::WithPay) source.nextExpected = parseDate(*nextText);
            if (potText) source.potId = potRef(ctx, *potText);
            const auto id = income.addSource(source);
            io.out() << "Added expected income #" << id << ".\n\n";
            printSources(io, income.sources(), today);
            return 0;
        }
    }

    printUsage(io);
    return 2;
}

} // namespace cli