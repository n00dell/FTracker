//
// Created by lee on 11/25/26.
//
// FTracker/cli/log_cmd.cpp
// The log: what happened, newest first.
#include <cli/commands.h>

#include <cli/display.h>

namespace cli {

    int runLog(Context& ctx, const Args& args) {
        Args rest = args;
        int limit = 50;
        if (const auto text = takeOption(rest, "--limit")) {
            const auto number = parseInt(*text);
            if (!number || *number < 1 || *number > 1000) throw pesa::Error("Give a limit from 1 to 1000.");
            limit = static_cast<int>(*number);
        }
        if (rest.size() > 2) { printUsage(ctx.io); return 2; }

        const std::string word = rest.size() == 2 ? toLower(rest[1]) : "all";
        pesa::LogFilter filter = pesa::LogFilter::All;
        if      (word == "all")     filter = pesa::LogFilter::All;
        else if (word == "auto")    filter = pesa::LogFilter::Auto;
        else if (word == "edits")   filter = pesa::LogFilter::Edits;
        else if (word == "savings") filter = pesa::LogFilter::Savings;
        else if (word == "skipped") filter = pesa::LogFilter::Skipped;
        else if (word == "issues")  filter = pesa::LogFilter::Issues;
        else throw pesa::Error("Use one of: all, auto, edits, savings, skipped, issues.");

        const auto events = ctx.app.log().list(filter, limit);
        printLog(ctx.io, events, pesa::Date::today());
        if (static_cast<int>(events.size()) == limit) {
            ctx.io.out() << "  Showing the newest " << limit << ". Use --limit for more.\n";
        }
        return 0;
    }

} // namespace cli