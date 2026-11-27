// FTracker/cli/item_cmd.cpp
#include <cli/commands.h>

#include <cli/display.h>
#include <cli/pickers.h>

namespace cli {

namespace {

pesa::CategoryId categoryRef(Context& ctx, const std::string& text) {
    if (const auto id = parseInt(text)) return *id;                  // "2"
    for (const auto& c : ctx.app.categories().list(pesa::Visibility::IncludeInactive)) {
        if (toLower(c.name) == toLower(text)) return c.id;           // "Transport"
    }
    throw pesa::Error("No category named '" + text + "'.");
}

pesa::ItemId itemIdArg(Context& ctx, ArgReader& reader, pesa::Visibility show, const std::string& label,
                       const ItemFilter& filter = {}) {
    if (const auto text = reader.next()) return parseId(*text, "item");
    return pickItem(ctx, std::nullopt, show, label, filter);
}

} // namespace

int runItem(Context& ctx, const Args& args) {
    auto& items = ctx.app.items();
    auto& io = ctx.io;

    Args rest = args;
    const auto untilText = takeOption(rest, "--until");
    const std::string sub = rest.size() >= 2
        ? rest[1]
        : chooseSubcommand(io, "Items", {{"Add an item", "add"}, {"List items", "list"},
                                         {"Rename an item", "rename"}, {"Change an amount", "amount"},
                                         {"Short-lived: set an end date", "until"}, {"Keep forever", "forever"},
                                         {"Pause an item", "deactivate"},
                                         {"Resume an item", "activate"}, {"Delete an item", "delete"},
                                         {"Restore a deleted item", "restore"}});
    ArgReader reader(rest, 2);

    if (sub == "add") {
        std::optional<pesa::CategoryId> category;
        std::string name;
        std::optional<pesa::Money> amount;

        if (const auto text = reader.next()) category = categoryRef(ctx, *text);
        const auto amountText = reader.next();
        const auto nameText = reader.rest();
        if (amountText) amount = parseAmount(*amountText);
        if (nameText) name = *nameText;

        std::vector<Step> steps;
        if (!category)   steps.push_back({[&] { category = pickCategory(ctx, pesa::Visibility::ActiveOnly, "Which category", {}, category); }});
        if (!nameText)   steps.push_back({[&] { name = io.askText("Item name", name.empty() ? std::nullopt : std::optional(name)); }});
        if (!amountText) steps.push_back({[&] { amount = io.askAmount("Default amount", amount); }});
        runSteps(steps);

        std::optional<pesa::Date> until;
        if (untilText) until = parseDate(*untilText);

        const auto id = items.add({.categoryId = *category, .name = name, .defaultAmount = *amount, .expiresOn = until});
        const auto item = items.find(id);
        io.out() << "Added '" << item->name << "' (#" << id << ") to " << item->categoryName << ": "
                 << formatMoney(item->defaultAmount) << (until ? ", in use until " + until->toString() : std::string())
                 << ". To post it automatically, put it in a rule ('rule add').\n";
        return 0;
    }
    if (sub == "list") {
        Args flags = rest;
        const auto show = takeVisibility(flags);
        std::optional<pesa::CategoryId> category;
        if (const auto text = reader.next(); text && !text->starts_with("--")) category = categoryRef(ctx, *text);
        printItems(io, items.list(category, show));
        return 0;
    }
    if (sub == "rename") {
        const auto id = itemIdArg(ctx, reader, pesa::Visibility::IncludeInactive, "Rename which item");
        const auto typed = reader.rest();
        items.rename(id, typed ? *typed : io.askText("New name"));
        io.out() << "Renamed.\n";
        return 0;
    }
    if (sub == "amount") {
        const auto id = itemIdArg(ctx, reader, pesa::Visibility::IncludeInactive, "Change which item");
        const auto typed = reader.next();
        const auto current = items.find(id);
        items.setDefaultAmount(id, typed ? parseAmount(*typed)
                                         : io.askAmount("New amount", current ? std::optional(current->defaultAmount)
                                                                              : std::nullopt));
        io.out() << "Amount updated. Days already posted keep their old amount.\n";
        return 0;
    }
    if (sub == "until") {
        const auto id = itemIdArg(ctx, reader, pesa::Visibility::IncludeInactive, "Which item ends");
        const auto text = reader.next();
        const pesa::Date last = text ? parseDate(*text) : io.askDate("Last day it is in use");
        items.setExpiry(id, last);
        io.out() << "It stops posting after " << last.toString() << " and pauses itself then.\n";
        return 0;
    }
    if (sub == "forever") {
        items.setExpiry(itemIdArg(ctx, reader, pesa::Visibility::IncludeInactive, "Which item"), std::nullopt);
        io.out() << "It stays until you pause or delete it.\n";
        return 0;
    }
    if (sub == "deactivate") {
        items.setActive(itemIdArg(ctx, reader, pesa::Visibility::IncludeInactive, "Pause which item",
                                  [](const pesa::Item& i) { return i.isActive; }), false);
        io.out() << "Paused. Its name stays reserved.\n";
        return 0;
    }
    if (sub == "activate") {
        items.setActive(itemIdArg(ctx, reader, pesa::Visibility::IncludeInactive, "Resume which item",
                                  [](const pesa::Item& i) { return !i.isActive; }), true);
        io.out() << "Resumed (and no longer short-lived).\n";
        return 0;
    }
    if (sub == "delete") {
        items.remove(itemIdArg(ctx, reader, pesa::Visibility::IncludeInactive, "Delete which item"));
        io.out() << "Deleted. Its name is free again; 'item restore' undoes this.\n";
        return 0;
    }
    if (sub == "restore") {
        items.restore(itemIdArg(ctx, reader, pesa::Visibility::DeletedOnly, "Restore which item"));
        io.out() << "Restored.\n";
        return 0;
    }
    printUsage(io);
    return 2;
}

} // namespace cli