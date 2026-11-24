// FTracker/cli/category_cmd.cpp
#include <cli/commands.h>

#include <cli/display.h>
#include <cli/pickers.h>

namespace cli {

namespace {
    // Misc is built in: it can't be renamed, paused or deleted, so never offer it for those.
    bool isEditable(const pesa::Category& c) { return !c.isSystem; }
// The id typed after the command, or a numbered list to pick from.
pesa::CategoryId categoryIdArg(Context& ctx, ArgReader& reader, pesa::Visibility show,
                               const std::string& label, const CategoryFilter& filter = {}) {
    if (const auto text = reader.next()) return parseId(*text, "category");
    return pickCategory(ctx, show, label, filter);
}

} // namespace

int runCategory(Context& ctx, const Args& args) {
    auto& categories = ctx.app.categories();
    auto& io = ctx.io;
    const std::string sub = args.size() >= 2
        ? args[1]
        : chooseSubcommand(io, "Categories", {{"Add a category", "add"}, {"List categories", "list"},
                                              {"Rename a category", "rename"}, {"Pause a category", "deactivate"},
                                              {"Resume a category", "activate"}, {"Delete a category", "delete"},
                                              {"Restore a deleted category", "restore"}});
    ArgReader reader(args, 2);

    if (sub == "add") {
        const auto typed = reader.rest();
        const std::string name = typed ? *typed : io.askText("Category name");
        const auto id = categories.add(name);
        io.out() << "Added category '" << categories.find(id)->name << "' (#" << id << ").\n";
        return 0;
    }
    if (sub == "list") {
        Args flags = args;
        printCategories(io, categories.list(takeVisibility(flags)));
        return 0;
    }
    if (sub == "rename") {
        const auto id = categoryIdArg(ctx, reader, pesa::Visibility::IncludeInactive, "Rename which category", isEditable);
        const auto typed = reader.rest();
        categories.rename(id, typed ? *typed : io.askText("New name"));
        io.out() << "Renamed.\n";
        return 0;
    }
    if (sub == "deactivate") {
        const auto id = categoryIdArg(ctx, reader, pesa::Visibility::IncludeInactive, "Pause which category",
                              [](const pesa::Category& c) { return c.isActive && !c.isSystem; });
        categories.setActive(id, false);
        io.out() << "Paused. Its name stays reserved and its items are hidden.\n";
        return 0;
    }
    if (sub == "activate") {
        const auto id = categoryIdArg(ctx, reader, pesa::Visibility::IncludeInactive, "Resume which category",
                                      [](const pesa::Category& c) { return !c.isActive; });
        categories.setActive(id, true);
        io.out() << "Resumed.\n";
        return 0;
    }
    if (sub == "delete") {
        const auto id = categoryIdArg(ctx, reader, pesa::Visibility::IncludeInactive, "Delete which category", isEditable);
        categories.remove(id);
        io.out() << "Deleted. Its name is free again; 'category restore' undoes this.\n";
        return 0;
    }
    if (sub == "restore") {
        const auto id = categoryIdArg(ctx, reader, pesa::Visibility::DeletedOnly, "Restore which category");
        categories.restore(id);
        io.out() << "Restored.\n";
        return 0;
    }
    printUsage(io);
    return 2;
}

} // namespace cli
