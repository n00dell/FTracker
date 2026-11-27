// FTracker/cli/entry_cmd.cpp
#include <cli/commands.h>

#include <cli/display.h>
#include <cli/pickers.h>

namespace cli {

namespace {

// The entry id typed, or: ask for a day, then pick from that day's entries.
pesa::EntryId entryIdArg(Context& ctx, ArgReader& reader, bool& guided) {
    if (const auto text = reader.next()) return parseId(*text, "entry");
    guided = true;
    return pickEntry(ctx, ctx.io.askDate("Which day", pesa::Date::today()));
}

pesa::Entry requireEntry(Context& ctx, pesa::EntryId id) {
    const auto entry = ctx.app.entries().find(id);
    if (!entry) throw pesa::Error("No entry with id " + std::to_string(id) + ".");
    return *entry;
}

} // namespace

int runEntry(Context& ctx, const Args& args) {
    auto& entries = ctx.app.entries();
    auto& io = ctx.io;

    Args rest = args;
    const bool savingsFlag = takeFlag(rest, "--savings");
    const std::string sub = rest.size() >= 2
        ? rest[1]
        : chooseSubcommand(io, "Entries", {{"Add an entry from the catalog", "add"}, {"Add a one-off", "oneoff"},
                                           {"Change an amount", "amount"}, {"Change earned/savings", "fund"},
                                           {"Delete an entry", "delete"}});
    ArgReader reader(rest, 2);
    pesa::FundedFrom fund = savingsFlag ? pesa::FundedFrom::Savings : pesa::FundedFrom::Earned;

    // The "paid from" question, as a step so < can come back to it.
    const auto sourceStep = [&](bool guided) {
        return Step{[&] {
            fund = io.askChoice("Paid from", {"Earned money", "Savings"}, fund == pesa::FundedFrom::Savings ? 1 : 0) == 1
                       ? pesa::FundedFrom::Savings : pesa::FundedFrom::Earned;
        }, [guided, savingsFlag] { return guided && !savingsFlag; }};
    };

    if (sub == "add") {
        pesa::Date date = pesa::Date::today();
        std::optional<pesa::CategoryId> category;
        pesa::ItemId itemId = 0;
        std::optional<pesa::Money> amount;

        const auto dateText = reader.next();
        const auto itemText = reader.next();
        const auto amountText = reader.next();
        if (dateText) date = parseDate(*dateText);
        if (itemText) itemId = parseId(*itemText, "item");
        if (amountText) amount = parseAmount(*amountText);
        const bool guided = !dateText || !itemText;     // anything missing -> we ask

        std::vector<Step> steps;
        if (!dateText) steps.push_back({[&] { date = io.askDate("Date", date); }});
        if (!itemText) {
            steps.push_back({[&] { category = pickCategory(ctx, pesa::Visibility::ActiveOnly, "Which category", {}, category); }});
            steps.push_back({[&] {
                itemId = pickItem(ctx, category, pesa::Visibility::ActiveOnly, "Which item");
                if (!amountText) amount.reset();          // a different item: its own default amount
            }});
        }
        if (guided && !amountText) {
            steps.push_back({[&] {
                const auto item = ctx.app.items().find(itemId);
                amount = io.askAmount("Amount", amount ? amount : item ? std::optional(item->defaultAmount) : std::nullopt);
            }});
        }
        steps.push_back(sourceStep(guided));
        runSteps(steps);

        const auto id = entries.addFromItem(date, itemId, amount, fund);
        io.out() << "Added entry #" << id << ".\n\n";
        printDay(io, ctx.app.days().view(date));
        autoCoverAfterChange(ctx);
        return 0;
    }

    if (sub == "oneoff") {
        pesa::Date date = pesa::Date::today();
        std::string name;
        std::optional<pesa::Money> amount;

        const auto dateText = reader.next();
        const auto amountText = reader.next();
        const auto nameText = reader.rest();
        if (dateText) date = parseDate(*dateText);
        if (amountText) amount = parseAmount(*amountText);
        if (nameText) name = *nameText;
        const bool guided = !dateText || !amountText || !nameText;

        std::vector<Step> steps;
        if (!dateText)   steps.push_back({[&] { date = io.askDate("Date", date); }});
        if (!nameText)   steps.push_back({[&] { name = io.askText("What was it for", name.empty() ? std::nullopt : std::optional(name)); }});
        if (!amountText) steps.push_back({[&] { amount = io.askAmount("Amount", amount); }});
        steps.push_back(sourceStep(guided));
        runSteps(steps);

        const auto id = entries.addOneOff(date, name, *amount, fund);
        io.out() << "Added one-off #" << id << " (for this day only; the catalog is unchanged).\n\n";
        printDay(io, ctx.app.days().view(date));
        autoCoverAfterChange(ctx);
        return 0;
    }

    if (sub == "amount" || sub == "fund" || sub == "delete") {
        bool guided = false;
        const auto id = entryIdArg(ctx, reader, guided);
        const auto before = requireEntry(ctx, id);

        if (sub == "amount") {
            const auto text = reader.next();
            entries.setAmount(id, text ? parseAmount(*text) : io.askAmount("New amount", before.amount));
            io.out() << "Amount updated.\n\n";
        } else if (sub == "fund") {
            std::string choice;
            if (const auto text = reader.next()) choice = toLower(*text);
            else choice = io.askChoice("Paid from", {"Earned money", "Savings"},
                                       before.fund == pesa::FundedFrom::Savings ? 1 : 0) == 1 ? "savings" : "earned";
            if (choice != "earned" && choice != "savings") throw pesa::Error("Use 'earned' or 'savings'.");
            entries.setFundedFrom(id, choice == "savings" ? pesa::FundedFrom::Savings : pesa::FundedFrom::Earned);
            io.out() << "Updated.\n\n";
        } else {
            if (guided && !io.askYesNo("Delete '" + before.name + "' (" + formatMoney(before.amount) + ")?", false)) {
                io.out() << "Kept it.\n";
                return 0;
            }
            entries.remove(id);
            io.out() << "Deleted.\n\n";
        }
        printDay(io, ctx.app.days().view(before.date));
        autoCoverAfterChange(ctx);
        return 0;
    }
    printUsage(io);
    return 2;
}

} // namespace cli