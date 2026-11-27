//
// FTracker/cli/rule_cmd.cpp
// Rules say WHEN items post. Items say WHAT and HOW MUCH.
#include <cli/commands.h>

#include <cli/display.h>
#include <cli/pickers.h>

#include <algorithm>

namespace cli {

namespace {

pesa::RuleId ruleIdArg(Context& ctx, ArgReader& reader, pesa::Visibility show, const std::string& label,
                       const RuleFilter& filter = {}) {
    if (const auto text = reader.next()) return parseId(*text, "rule");
    return pickRule(ctx, show, label, filter);
}

pesa::Rule requireRule(Context& ctx, pesa::RuleId id) {
    const auto rule = ctx.app.rules().find(id);
    if (!rule) throw pesa::Error("No rule with id " + std::to_string(id) + ".");
    return *rule;
}

pesa::CategoryId categoryRef(Context& ctx, const std::string& text) {
    if (const auto id = parseInt(text)) return *id;
    for (const auto& c : ctx.app.categories().list(pesa::Visibility::IncludeInactive)) {
        if (toLower(c.name) == toLower(text)) return c.id;
    }
    throw pesa::Error("No category named '" + text + "'.");
}

// Item ids typed as "3" or as "1,2,3".
void addItemIds(std::vector<pesa::ItemId>& out, const std::string& text) {
    std::size_t start = 0;
    while (true) {
        const auto comma = text.find(',', start);
        const std::string part = text.substr(start, comma == std::string::npos ? std::string::npos : comma - start);
        if (!part.empty()) out.push_back(parseId(part, "item"));
        if (comma == std::string::npos) break;
        start = comma + 1;
    }
}

// Every active item in a category, as it is now. (Items added to the category later are not included.)
void addCategoryItems(Context& ctx, std::vector<pesa::ItemId>& out, pesa::CategoryId category) {
    const auto found = ctx.app.items().list(category, pesa::Visibility::ActiveOnly);
    if (found.empty()) {
        const auto info = ctx.app.categories().find(category);
        throw pesa::Error("The category '" + (info ? info->name : std::to_string(category)) + "' has no items yet.");
    }
    for (const auto& item : found) out.push_back(item.id);
}

void removeDuplicates(std::vector<pesa::ItemId>& ids) {
    std::vector<pesa::ItemId> unique;
    for (const pesa::ItemId id : ids) {
        if (std::ranges::find(unique, id) == unique.end()) unique.push_back(id);
    }
    ids = std::move(unique);
}

// Asks until the answer makes a valid schedule. Backing out (Cancelled) still escapes.
pesa::Schedule askSchedule(Console& io) {
    const auto kind = io.askChoice("How often does it repeat",
                                   {"On certain weekdays", "Once a month, on a day number"}, 0);
    while (true) {
        try {
            if (kind == 0) {
                return pesa::Schedule::weekly(io.askDays("Days it posts", pesa::WeekdaySet::workDays()));
            }
            return pesa::Schedule::monthly(static_cast<int>(io.askAmount("Day of the month (1-31)")));
        } catch (const pesa::Error& e) {
            io.out() << "  " << e.what() << "\n";
        }
    }
}

} // namespace

int runRule(Context& ctx, const Args& args) {
    auto& rules = ctx.app.rules();
    auto& io = ctx.io;

    // Options can be typed anywhere; pull them out first so the rest is just words.
    Args rest = args;
    const auto daysOption    = takeOption(rest, "--days");
    const auto monthlyOption = takeOption(rest, "--monthly");
    const auto fromOption    = takeOption(rest, "--from");
    const auto toOption      = takeOption(rest, "--to");
    const bool yes = takeFlag(rest, "--yes");

    // Items: --item 3 (repeatable), --items 1,2,3, and --category Food (all of its items, as they are now).
    std::vector<pesa::ItemId> itemIds;
    while (const auto text = takeOption(rest, "--item"))  addItemIds(itemIds, *text);
    while (const auto text = takeOption(rest, "--items")) addItemIds(itemIds, *text);
    while (const auto text = takeOption(rest, "--category")) addCategoryItems(ctx, itemIds, categoryRef(ctx, *text));
    removeDuplicates(itemIds);

    const std::string sub = rest.size() >= 2
        ? rest[1]
        : chooseSubcommand(io, "Rules", {{"Add a rule", "add"}, {"Add an adjust rule (a price change)", "adjust"},
                                         {"List rules", "list"},
                                         {"See when a rule will post", "preview"},
                                         {"Rename a rule", "rename"}, {"Add items to a rule", "additem"},
                                         {"Remove items from a rule", "removeitem"},
                                         {"End a rule on a date", "end"}, {"Pause a rule", "deactivate"},
                                         {"Resume a rule", "activate"}, {"Delete a rule", "delete"},
                                         {"Restore a deleted rule", "restore"}});
    ArgReader reader(rest, 2);

    if (sub == "add") {
        bool guided = false;   // anything missing -> we ask for it

        const auto typed = reader.rest();
        std::string name;
        if (typed) name = *typed; else { guided = true; name = io.askText("Rule name (e.g. Workdays commute)"); }

        if (daysOption && monthlyOption) throw pesa::Error("Use either --days or --monthly, not both.");
        pesa::Schedule schedule;
        if (daysOption) {
            schedule = pesa::Schedule::weekly(pesa::WeekdaySet::parse(*daysOption));
        } else if (monthlyOption) {
            const auto day = parseInt(*monthlyOption);
            if (!day) throw pesa::Error("--monthly needs a day number from 1 to 31.");
            schedule = pesa::Schedule::monthly(static_cast<int>(*day));
        } else {
            guided = true;
            schedule = askSchedule(io);
        }

        if (itemIds.empty()) { guided = true; itemIds = pickItems(ctx, "Which items does it post"); }

        pesa::Date from = pesa::Date::today();
        if (fromOption) from = parseDate(*fromOption);
        else if (guided) from = io.askDate("Starts on", from);

        std::optional<pesa::Date> to;
        if (toOption) to = parseDate(*toOption);
        else if (guided && io.askYesNo("Does it stop on a set date?", false)) to = io.askDate("Last day it applies");

        const auto id = rules.add({.name = name, .schedule = schedule, .effectiveFrom = from,
                                   .effectiveTo = to, .itemIds = itemIds});
        io.out() << "Added rule #" << id << " with " << itemIds.size() << (itemIds.size() == 1 ? " item" : " items") << ".\n\n";
        printRules(io, {requireRule(ctx, id)});
        return 0;
    }
    if (sub == "adjust") {
        bool guided = false;

        const auto changeText = reader.next();
        const auto typed = reader.rest();
        pesa::Money change = 0;
        if (changeText) change = parseAmount(*changeText);
        else { guided = true; change = parseAmount(io.askText("Change per posting in KES (for example 10, or -10)")); }

        std::string name;
        if (typed) name = *typed; else { guided = true; name = io.askText("Rule name (e.g. Tea price rise)"); }

        if (itemIds.empty()) { guided = true; itemIds = pickItems(ctx, "Which items does it change"); }

        const pesa::Date today = pesa::Date::today();
        pesa::Date from = today;
        if (fromOption) from = parseDate(*fromOption);
        else if (guided) from = io.askDate("Starts on", today);

        std::optional<pesa::Date> to;
        if (toOption) to = parseDate(*toOption);
        else if (guided && io.askYesNo("Does it stop on a set date?", false)) to = io.askDate("Last day it applies");

        const pesa::NewRule adjust{.name = name, .effectiveFrom = from, .effectiveTo = to, .itemIds = itemIds,
                                   .kind = pesa::RuleKind::Adjust, .extra = change};
        const auto preview = rules.addAdjustment(adjust, today, pesa::Mode::Preview);
        if (!confirmChange(ctx, preview.changes, "This rule changes", yes)) return 0;

        const auto done = rules.addAdjustment(adjust, today);
        io.out() << "Added rule #" << done.created << ".\n";
        if (done.restorePoint) {
            io.out() << "Restore point #" << *done.restorePoint << " saved. 'restore " << *done.restorePoint
                     << "' goes back.\n";
        }
        return 0;
    }
    if (sub == "list") {
        Args flags = rest;
        printRules(io, rules.list(takeVisibility(flags)));
        return 0;
    }
    if (sub == "preview") {
        const auto id = ruleIdArg(ctx, reader, pesa::Visibility::IncludeInactive, "Preview which rule");
        std::int64_t count = 30;
        if (const auto text = reader.next()) {
            const auto number = parseInt(*text);
            if (!number || *number < 1 || *number > 366) throw pesa::Error("Give a number of days from 1 to 366.");
            count = *number;
        }

        const auto rule = requireRule(ctx, id);
        if (rule.kind == pesa::RuleKind::Adjust) {
            throw pesa::Error("Preview is for charge rules. An adjust rule changes amounts; it does not post.");
        }
        Table table({{"Date"}, {"Day"}, {"Posts"}, {"Total", Table::Align::Right}});
        const auto today = pesa::Date::today();
        std::size_t matches = 0;
        for (int offset = 0; offset < count; ++offset) {
            const auto date = today.addDays(offset);
            if (!rule.appliesOn(date)) continue;
            ++matches;

            std::string posts;
            pesa::Money total = 0;
            for (const auto& ruleItem : rule.items) {
                const auto item = ctx.app.items().find(ruleItem.id);
                if (!item) continue;
                posts += (posts.empty() ? "" : ", ") + item->name + " " + formatMoney(item->defaultAmount);
                total += item->defaultAmount;
            }
            table.addRow({date.toString(), weekdayName(date.weekday()), posts, formatMoney(total)});
        }
        io.out() << "'" << rule.name << "' (" << rule.schedule.toString() << ") over the next " << count << " days:\n";
        if (matches == 0) io.out() << "  It does not post in that time"
                                   << (rule.isActive ? "" : " (the rule is paused)") << ".\n";
        else table.print(io);
        return 0;
    }
    if (sub == "rename") {
        const auto id = ruleIdArg(ctx, reader, pesa::Visibility::IncludeInactive, "Rename which rule");
        const auto typed = reader.rest();
        rules.rename(id, typed ? *typed : io.askText("New name"));
        io.out() << "Renamed.\n";
        return 0;
    }
    if (sub == "additem") {
        const auto id = ruleIdArg(ctx, reader, pesa::Visibility::IncludeInactive, "Add items to which rule");
        std::vector<pesa::ItemId> more = itemIds;                       // from --item / --items / --category
        while (const auto text = reader.next()) addItemIds(more, *text);   // rule additem 1 3 4   or   rule additem 1 3,4
        if (more.empty()) more = pickItems(ctx, "Which items to add");
        removeDuplicates(more);
        rules.addItems(id, more);
        io.out() << "Added " << more.size() << (more.size() == 1 ? " item" : " items") << ".\n\n";
        printRules(io, {requireRule(ctx, id)});
        return 0;
    }
    if (sub == "removeitem") {
        const auto id = ruleIdArg(ctx, reader, pesa::Visibility::IncludeInactive, "Remove items from which rule");
        const auto rule = requireRule(ctx, id);
        std::vector<pesa::ItemId> gone = itemIds;
        while (const auto text = reader.next()) addItemIds(gone, *text);
        if (gone.empty()) {
            if (rule.items.empty()) throw pesa::Error("This rule has no items.");
            std::vector<std::string> names;
            for (const auto& ruleItem : rule.items) names.push_back(ruleItem.name);
            gone.push_back(rule.items[io.askChoice("Remove which item", names)].id);
        }
        removeDuplicates(gone);
        rules.removeItems(id, gone);
        io.out() << "Removed. Entries already posted are not touched.\n\n";
        printRules(io, {requireRule(ctx, id)});
        return 0;
    }
    if (sub == "end") {
        const auto id = ruleIdArg(ctx, reader, pesa::Visibility::IncludeInactive, "End which rule");
        const auto text = reader.next();
        rules.endOn(id, text ? parseDate(*text) : io.askDate("Last day it applies"));
        io.out() << "Done. To change WHEN something posts, end the old rule and add a new one from a new date.\n";
        return 0;
    }
    if (sub == "deactivate") {
        rules.setActive(ruleIdArg(ctx, reader, pesa::Visibility::IncludeInactive, "Pause which rule",
                                  [](const pesa::Rule& r) { return r.isActive; }), false);
        io.out() << "Paused. Days already posted are not touched.\n";
        return 0;
    }
    if (sub == "activate") {
        rules.setActive(ruleIdArg(ctx, reader, pesa::Visibility::IncludeInactive, "Resume which rule",
                                  [](const pesa::Rule& r) { return !r.isActive; }), true);
        io.out() << "Resumed.\n";
        return 0;
    }
    if (sub == "delete") {
        rules.remove(ruleIdArg(ctx, reader, pesa::Visibility::IncludeInactive, "Delete which rule"));
        io.out() << "Deleted. 'rule restore' undoes this.\n";
        return 0;
    }
    if (sub == "restore") {
        rules.restore(ruleIdArg(ctx, reader, pesa::Visibility::DeletedOnly, "Restore which rule"));
        io.out() << "Restored.\n";
        return 0;
    }
    printUsage(io);
    return 2;
}

} // namespace cli