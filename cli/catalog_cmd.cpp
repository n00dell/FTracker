// FTracker/cli/catalog_cmd.cpp
// The catalog is one place for categories AND the items inside them. These commands
// only gather choices, then hand over to the category/item commands: no logic twice.
#include <cli/commands.h>

#include <cli/display.h>
#include <cli/pickers.h>

namespace cli {
    namespace {
        void showCatalog(Context &ctx, pesa::Visibility show) {
            printCatalog(ctx.io, ctx.app.categories().list(show), ctx.app.items().list(std::nullopt, show));
        }

        struct Target {
            bool isCategory;
            std::int64_t id;
            bool isActive;
            bool isSystem; // the built-in Misc category: it can only receive items
        };

        int editCatalog(Context &ctx) {
            auto &io = ctx.io;
            const auto categories = ctx.app.categories().list(pesa::Visibility::IncludeInactive);
            const auto items = ctx.app.items().list(std::nullopt, pesa::Visibility::IncludeInactive);

            std::vector<Target> targets;
            std::vector<std::string> labels;
            for (const auto &c: categories) {
                targets.push_back({true, c.id, c.isActive, c.isSystem});
                labels.push_back(
                    c.name + "  (category)" + (c.isActive ? "" : "  (paused)") + (c.isSystem ? "  (built in)" : ""));
                for (const auto &i: items) {
                    if (i.categoryId != c.id) continue;
                    targets.push_back({false, i.id, i.isActive, false});
                    labels.push_back("    " + i.name + "  " + formatMoney(i.defaultAmount) +
                                     (i.isActive ? "" : "  (paused)"));
                }
            }

            const Target target = targets[io.askChoice("Pick a category or an item", labels)];
            const std::string id = std::to_string(target.id);
            const std::string pauseWord = target.isActive ? "Pause" : "Resume";
            const std::string pauseSub = target.isActive ? "deactivate" : "activate";

            if (target.isCategory) {
                const std::string sub = target.isSystem
                                            ? chooseSubcommand(io, "This category", {{"Add an item to it", "add-item"}})
                                            : chooseSubcommand(io, "This category",
                                                               {
                                                                   {"Rename", "rename"},
                                                                   {"Add an item to it", "add-item"},
                                                                   {pauseWord, pauseSub}, {"Delete", "delete"}
                                                               });
                if (sub == "add-item") runItem(ctx, {"item", "add", id});
                else runCategory(ctx, {"category", sub, id});
            } else {
                const std::string sub = chooseSubcommand(io, "This item",
                                                         {
                                                             {"Rename", "rename"}, {"Change the amount", "amount"},
                                                             {pauseWord, pauseSub}, {"Delete", "delete"}
                                                         });
                runItem(ctx, {"item", sub, id});
            }
            io.out() << "\n";
            showCatalog(ctx, pesa::Visibility::IncludeInactive);
            return 0;
        }
    } // namespace

    int runCatalog(Context &ctx, const Args &args) {
        const std::string sub = (args.size() >= 2 && !args[1].starts_with("--")) ? args[1] : "show";

        if (sub == "show") {
            Args flags = args;
            showCatalog(ctx, takeVisibility(flags));
            return 0;
        }
        if (sub == "add") {
            const auto what = ctx.io.askChoice("What do you want to add", {"A category", "An item"});
            return what == 0 ? runCategory(ctx, {"category", "add"}) : runItem(ctx, {"item", "add"});
        }
        if (sub == "edit") return editCatalog(ctx);

        printUsage(ctx.io);
        return 2;
    }
} // namespace cli
