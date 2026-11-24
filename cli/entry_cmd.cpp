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

    auto askSource = [&](bool guided) {
        if (guided && !savingsFlag) {
            fund = io.askChoice("Paid from", {"Earned money", "Savings"}, 0) == 1 ? pesa::FundedFrom::Savings
                                                                                  : pesa::FundedFrom::Earned;
        }
    };

    if (sub == "add") {
        bool guided = false;

        pesa::Date date;
        if (const auto text = reader.next()) date = parseDate(*text);
        else { guided = true; date = io.askDate("Date", pesa::Date::today()); }

        pesa::ItemId itemId = 0;
        if (const auto text = reader.next()) itemId = parseId(*text, "item");
        else {
            guided = true;
            const auto category = pickCategory(ctx, pesa::Visibility::ActiveOnly, "Which category");
            itemId = pickItem(ctx, category, pesa::Visibility::ActiveOnly, "Which item");
        }

        std::optional<pesa::Money> amount;
        if (const auto text = reader.next()) amount = parseAmount(*text);
        else if (guided) {
            const auto item = ctx.app.items().find(itemId);
            amount = io.askAmount("Amount", item ? std::optional(item->defaultAmount) : std::nullopt);
        }
        askSource(guided);

        const auto id = entries.addFromItem(date, itemId, amount, fund);
        io.out() << "Added entry #" << id << ".\n\n";
        printDay(io, ctx.app.days().view(date));
        return 0;
    }

    if (sub == "oneoff") {
        bool guided = false;

        pesa::Date date;
        if (const auto text = reader.next()) date = parseDate(*text);
        else { guided = true; date = io.askDate("Date", pesa::Date::today()); }

        const auto amountText = reader.next();
        const auto nameText = reader.rest();

        std::string name;
        if (nameText) name = *nameText; else { guided = true; name = io.askText("What was it for"); }

        pesa::Money amount = 0;
        if (amountText) amount = parseAmount(*amountText);
        else { guided = true; amount = io.askAmount("Amount"); }
        askSource(guided);

        const auto id = entries.addOneOff(date, name, amount, fund);
        io.out() << "Added one-off #" << id << " (for this day only; the catalog is unchanged).\n\n";
        printDay(io, ctx.app.days().view(date));
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
        return 0;
    }
    printUsage(io);
    return 2;
}

} // namespace cli