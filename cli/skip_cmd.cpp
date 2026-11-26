//
// Created by lee on 11/26/26.
//
// FTracker/cli/skip_cmd.cpp
// Skip days, wipe all and restore points: the commands that change days already posted.
// Each one shows a preview first and only goes ahead when you say yes.
#include <cli/commands.h>

#include <cli/display.h>
#include <cli/pickers.h>

namespace cli {
    namespace {
        pesa::CategoryId categoryRef(Context &ctx, const std::string &text) {
            if (const auto id = parseInt(text)) return *id;
            for (const auto &c: ctx.app.categories().list(pesa::Visibility::IncludeInactive)) {
                if (toLower(c.name) == toLower(text)) return c.id;
            }
            throw pesa::Error("No category named '" + text + "'.");
        }

        void tellRestorePoint(Console &io, const pesa::CorrectionResult &result) {
            if (result.restorePoint) {
                io.out() << "Restore point #" << *result.restorePoint << " saved. 'restore " << *result.restorePoint
                        << "' goes back.\n";
            }
        }
    } // namespace

    int runSkip(Context &ctx, const Args &args) {
        auto &io = ctx.io;

        Args rest = args;
        const bool yes = takeFlag(rest, "--yes");
        const auto fromText = takeOption(rest, "--from");
        const auto toText = takeOption(rest, "--to");
        std::vector<pesa::CategoryId> categories;
        std::vector<pesa::ItemId> items;
        while (const auto text = takeOption(rest, "--category")) categories.push_back(categoryRef(ctx, *text));
        while (const auto text = takeOption(rest, "--item")) items.push_back(parseId(*text, "item"));

        const std::string sub = rest.size() >= 2
                                    ? rest[1]
                                    : chooseSubcommand(io, "Skips", {
                                                           {"Mark days to skip", "add"}, {"List skips", "list"},
                                                           {"Remove a skip", "remove"}
                                                       });
        ArgReader reader(rest, 2);
        const pesa::Date today = pesa::Date::today();

        if (sub == "list") {
            printSkips(io, ctx.app.skips().list());
            return 0;
        }

        if (sub == "add") {
            bool guided = false;
            const auto typed = reader.rest();
            std::string name;
            if (typed) name = *typed;
            else {
                guided = true;
                name = io.askText("Name (e.g. Public holiday)");
            }

            pesa::Date from;
            if (fromText) from = parseDate(*fromText);
            else {
                guided = true;
                from = io.askDate("First day", today);
            }
            pesa::Date to = from;
            if (toText) to = parseDate(*toText);
            else if (guided) to = io.askDate("Last day", from);

            if (categories.empty() && items.empty()) {
                const auto what = io.askChoice("What should it skip", {"A whole category", "One item"});
                if (what == 0) categories.push_back(pickCategory(ctx, pesa::Visibility::ActiveOnly, "Which category"));
                else items.push_back(pickItem(ctx, std::nullopt, pesa::Visibility::ActiveOnly, "Which item"));
            }

            const pesa::NewSkip skip{.name = name, .from = from, .to = to, .categories = categories, .items = items};
            const auto preview = ctx.app.skips().add(skip, today, pesa::Mode::Preview);
            if (!confirmChange(ctx, preview.changes, "Adding this skip marks as skipped", yes)) return 0;

            const auto done = ctx.app.skips().add(skip, today);
            io.out() << "Added skip #" << done.created << ".\n";
            tellRestorePoint(io, done);
            return 0;
        }

        if (sub == "remove") {
            pesa::SkipId id = 0;
            if (const auto text = reader.next()) {
                id = parseId(*text, "skip");
            } else {
                const auto skips = ctx.app.skips().list();
                if (skips.empty()) throw pesa::Error("There are no skips to remove.");
                std::vector<std::string> labels;
                for (const auto &s: skips) {
                    labels.push_back(
                        s.name + "  " + s.from.toString() + (s.from == s.to ? "" : " to " + s.to.toString()));
                }
                id = skips[io.askChoice("Remove which skip", labels)].id;
            }
            const auto preview = ctx.app.skips().remove(id, pesa::Mode::Preview);
            if (!confirmChange(ctx, preview.changes, "Removing this skip brings back", yes)) return 0;

            const auto done = ctx.app.skips().remove(id);
            io.out() << "Removed.\n";
            tellRestorePoint(io, done);
            return 0;
        }

        printUsage(io);
        return 2;
    }

    int runWipe(Context &ctx, const Args &args) {
        auto &io = ctx.io;
        Args rest = args;
        const bool yes = takeFlag(rest, "--yes");
        if (rest.size() > 3) {
            printUsage(io);
            return 2;
        }
        ArgReader reader(rest, 1);
        const pesa::Date today = pesa::Date::today();

        const auto firstText = reader.next();
        const auto lastText = reader.next();
        const pesa::Date from = firstText ? parseDate(*firstText) : io.askDate("First day to wipe", today.addDays(-6));
        const pesa::Date to = lastText ? parseDate(*lastText) : io.askDate("Last day to wipe", today.previousDay());

        const auto preview = ctx.app.days().wipe(from, to, today, pesa::Mode::Preview);
        if (preview.changes.entries == 0) {
            io.out() << "Nothing to wipe: no untouched rule-posted entries from " << from.toString() << " to "
                    << to.toString() << ".\n";
            return 0;
        }
        if (!confirmChange(ctx, preview.changes, "Wipe all deletes", yes)) return 0;

        const auto done = ctx.app.days().wipe(from, to, today);
        io.out() << "Deleted " << done.changes.entries << " entries.\n";
        tellRestorePoint(io, done);
        return 0;
    }

    int runRestore(Context &ctx, const Args &args) {
        auto &io = ctx.io;
        Args rest = args;
        const bool yes = takeFlag(rest, "--yes");
        auto &points = ctx.app.restorePoints();

        if (rest.size() == 1) {
            printRestorePoints(io, points.list());
            return 0;
        }
        if (rest.size() > 2) {
            printUsage(io);
            return 2;
        }

        const pesa::RestorePointId id = parseId(rest[1], "restore point");
        std::optional<pesa::RestorePoint> point;
        for (auto &p: points.list()) {
            if (p.id == id) point = std::move(p);
        }
        if (!point) throw pesa::Error("No restore point with id " + std::to_string(id) + ". See 'restore'.");

        io.out() << "Going back to '" << point->label << "' puts " << point->from.toString() << " to "
                << point->to.toString() << " back as they were (" << point->entries << " entries), and brings rules\n"
                << "and skips back to how they were then. Entries you added to those days since are lost, but Pesa\n"
                << "saves a restore point first, so this can be undone.\n";
        if (!yes && !io.askYesNo("Go ahead?", false)) {
            io.out() << "Nothing was changed.\n";
            return 0;
        }

        const auto result = points.restore(id);
        io.out() << "Put back " << result.entries << " entries.\n";
        if (result.undoPoint) io.out() << "To undo this: 'restore " << *result.undoPoint << "'.\n";
        return 0;
    }
} // namespace cli
