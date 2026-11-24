// FTracker/cli/include/cli/commands.h
#pragma once

#include <cli/console.h>
#include <cli/text.h>

#include <pesa/pesa.h>

namespace cli {

    // What every command needs: the app to talk to and the console to talk through.
    struct Context {
        pesa::Pesa& app;
        Console&    io;
    };

    void printUsage(Console& io);

    // args[0] is the command word ("item", "day", ...). Returns an exit code.
    int dispatch(Context& ctx, const Args& args);
    // Same, but turns errors and cancelling into a message plus an exit code.
    int runGuarded(Context& ctx, const Args& args);
    // The interactive "pesa>" prompt.
    int runShell(Context& ctx);

    int runCatalog(Context& ctx, const Args& args);
    int runCategory(Context& ctx, const Args& args);
    int runItem(Context& ctx, const Args& args);
    int runEntry(Context& ctx, const Args& args);
    int runDay(Context& ctx, const Args& args);
    int runRule(Context& ctx, const Args& args);
    int runSetup(Context& ctx, const Args& args);
    int runSettings(Context& ctx, const Args& args);
    int runJob(Context& ctx, const Args& args);
    void runStartup(Context& ctx, bool interactive);
} // namespace cli
