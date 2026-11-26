// FTracker/cli/include/cli/pickers.h
// "Show a numbered list and let me choose" for the things commands ask about,
// so nobody has to remember an id.
#pragma once

#include <cli/commands.h>

#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace cli {
    using CategoryFilter = std::function<bool(const pesa::Category &)>;
    using ItemFilter = std::function<bool(const pesa::Item &)>;
    using RuleFilter = std::function<bool(const pesa::Rule &)>;

    // `preselect` is the category chosen if the person just presses Enter.
    pesa::CategoryId pickCategory(Context &ctx, pesa::Visibility show, const std::string &label,
                                  const CategoryFilter &filter = {},
                                  std::optional<pesa::CategoryId> preselect = std::nullopt);

    pesa::ItemId pickItem(Context &ctx, std::optional<pesa::CategoryId> category,
                          pesa::Visibility show, const std::string &label,
                          const ItemFilter &filter = {});

    pesa::EntryId pickEntry(Context &ctx, const pesa::Date &date); // from one day's entries
    pesa::RuleId pickRule(Context &ctx, pesa::Visibility show, const std::string &label,
                          const RuleFilter &filter = {});

    // Several items, one at a time, until the person chooses "Done". At least one is required.
    std::vector<pesa::ItemId> pickItems(Context &ctx, const std::string &label);

    // "Categories - what do you want to do?" -> the chosen subcommand word.
    std::string chooseSubcommand(Console &io, const std::string &title,
                                 const std::vector<std::pair<std::string, std::string> > &options);

    // Shows what a bulk change would do and asks "Go ahead?". Returns true to continue. Says nothing
    // (and continues) when nothing posted is affected. `assumeYes` is --yes.
    bool confirmChange(Context &ctx, const pesa::CorrectionPreview &preview, const std::string &headline,
                       bool assumeYes);
} // namespace cli
