// FTracker/cli/pickers.cpp
#include <cli/pickers.h>

#include <cli/display.h>

#include <algorithm>

namespace cli {

namespace {

std::string suffix(bool isActive, bool isDeleted) {
    if (isDeleted) return "  (deleted)";
    if (!isActive) return "  (paused)";
    return "";
}

} // namespace

pesa::CategoryId pickCategory(Context& ctx, pesa::Visibility show, const std::string& label,
                              const CategoryFilter& filter, std::optional<pesa::CategoryId> preselect) {
    std::vector<pesa::Category> categories;
    for (const auto& c : ctx.app.categories().list(show)) {
        if (!filter || filter(c)) categories.push_back(c);
    }
    if (categories.empty()) throw pesa::Error("There are no categories to choose from.");

    std::vector<std::string> options;
    std::optional<std::size_t> preselected;   // position in the list, so Enter can choose it
    for (std::size_t i = 0; i < categories.size(); ++i) {
        options.push_back(categories[i].name + suffix(categories[i].isActive, categories[i].isDeleted));
        if (preselect && categories[i].id == *preselect) preselected = i;
    }
    return categories[ctx.io.askChoice(label, options, preselected)].id;
}

pesa::ItemId pickItem(Context& ctx, std::optional<pesa::CategoryId> category, pesa::Visibility show,
                      const std::string& label, const ItemFilter& filter) {
    std::vector<pesa::Item> items;
    for (const auto& i : ctx.app.items().list(category, show)) {
        if (!filter || filter(i)) items.push_back(i);
    }
    if (items.empty()) throw pesa::Error("There are no items to choose from.");

    std::vector<std::string> options;
    for (const auto& i : items) {
        std::string text = i.name;
        if (!category) text += "  (" + i.categoryName + ")";
        text += "  " + formatMoney(i.defaultAmount) + suffix(i.isActive, i.isDeleted);
        options.push_back(text);
    }
    return items[ctx.io.askChoice(label, options)].id;
}

pesa::RuleId pickRule(Context& ctx, pesa::Visibility show, const std::string& label, const RuleFilter& filter) {
    std::vector<pesa::Rule> rules;
    for (const auto& r : ctx.app.rules().list(show)) {
        if (!filter || filter(r)) rules.push_back(r);
    }
    if (rules.empty()) throw pesa::Error("There are no rules to choose from.");

    std::vector<std::string> options;
    for (const auto& r : rules) {
        options.push_back(r.name + "  " + r.schedule.toString() + suffix(r.isActive, r.isDeleted));
    }
    return rules[ctx.io.askChoice(label, options)].id;
}

std::vector<pesa::ItemId> pickItems(Context& ctx, const std::string& label) {
    const auto all = ctx.app.items().list(std::nullopt, pesa::Visibility::ActiveOnly);
    if (all.empty()) throw pesa::Error("There are no items yet. Add one with 'item add' first.");

    std::vector<pesa::ItemId> chosen;
    while (true) {
        // The items not picked yet, in the order they are shown.
        std::vector<const pesa::Item*> available;
        for (const auto& item : all) {
            if (std::ranges::find(chosen, item.id) == chosen.end()) available.push_back(&item);
        }
        if (available.empty()) break;   // everything has been picked

        std::vector<std::string> options;
        for (const auto* item : available) {
            options.push_back(item->name + "  (" + item->categoryName + ")  " + formatMoney(item->defaultAmount));
        }
        // Once one item is chosen, "Done" appears at the end and Enter selects it.
        const bool canFinish = !chosen.empty();
        if (canFinish) options.push_back("Done");

        const std::size_t picked = ctx.io.askChoice(
            label + " (" + std::to_string(chosen.size()) + " chosen)", options,
            canFinish ? std::optional<std::size_t>(available.size()) : std::nullopt);
        if (canFinish && picked == available.size()) break;
        chosen.push_back(available[picked]->id);
    }
    return chosen;
}

pesa::EntryId pickEntry(Context& ctx, const pesa::Date& date) {
    const auto entries = ctx.app.days().view(date).entries;
    if (entries.empty()) throw pesa::Error("There are no entries on " + date.toString() + ".");

    std::vector<std::string> options;
    for (const auto& e : entries) {
        options.push_back(entryCategoryText(e) + " / " + e.name + "  " + formatMoney(e.amount) + "  " +
                          (e.fund == pesa::FundedFrom::Earned ? "earned" : "savings") + "  " +
                          entryStateText(e));
    }
    return entries[ctx.io.askChoice("Entries on " + date.toString(), options)].id;
}

std::string chooseSubcommand(Console& io, const std::string& title,
                             const std::vector<std::pair<std::string, std::string>>& options) {
    std::vector<std::string> labels;
    for (const auto& option : options) labels.push_back(option.first);
    return options[io.askChoice(title + " - what do you want to do?", labels)].second;
}

} // namespace cli
