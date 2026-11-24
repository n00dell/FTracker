//
// Created by lee on 11/24/26.
//
// FTracker/cli/rule_cmd.cpp
// Rules say WHEN items post. Items say WHAT and HOW MUCH.
#include <cli/commands.h>

#include <cli/display.h>
#include <cli/pickers.h>

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
    std::vector<pesa::ItemId> itemIds;
    while (const auto text = takeOption(rest, "--item")) itemIds.push_back(parseId(*text, "item"));

    const std::string sub = rest.size() >= 2
        ? rest[1]
        : chooseSubcommand(io, "Rules", {{"Add a rule", "add"}, {"List rules", "list"},
                                         {"See when a rule will post", "preview"},
                                         {"Rename a rule", "rename"}, {"Add an item to a rule", "additem"},
                                         {"Remove an item from a rule", "removeitem"},
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
        io.out() << "Added rule #" << id << ".\n\n";
        printRules(io, {requireRule(ctx, id)});
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
        const auto id = ruleIdArg(ctx, reader, pesa::Visibility::IncludeInactive, "Add an item to which rule");
        pesa::ItemId item = 0;
        if (const auto text = reader.next()) item = parseId(*text, "item");
        else item = pickItem(ctx, std::nullopt, pesa::Visibility::ActiveOnly, "Which item");
        rules.addItem(id, item);
        io.out() << "Added.\n\n";
        printRules(io, {requireRule(ctx, id)});
        return 0;
    }
    if (sub == "removeitem") {
        const auto id = ruleIdArg(ctx, reader, pesa::Visibility::IncludeInactive, "Remove an item from which rule");
        const auto rule = requireRule(ctx, id);
        pesa::ItemId item = 0;
        if (const auto text = reader.next()) {
            item = parseId(*text, "item");
        } else {
            if (rule.items.empty()) throw pesa::Error("This rule has no items.");
            std::vector<std::string> names;
            for (const auto& ruleItem : rule.items) names.push_back(ruleItem.name);
            item = rule.items[io.askChoice("Remove which item", names)].id;
        }
        rules.removeItem(id, item);
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
