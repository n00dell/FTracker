// FTracker/cli/day_cmd.cpp
#include <cli/commands.h>

#include <cli/display.h>

namespace cli {
    int runDay(Context &ctx, const Args &args) {
        if (args.size() > 2) {
            printUsage(ctx.io);
            return 2;
        }
        const pesa::Date date = args.size() == 2 ? parseDate(args[1]) : pesa::Date::today();
        printDay(ctx.io, ctx.app.days().view(date));
        return 0;
    }
} // namespace cli
