//
// Created by lee on 11/24/26.
//
// FTracker/cli/startup.cpp
// What happens every time Pesa opens.
#include <cli/commands.h>

namespace cli {

    void runStartup(Context& ctx, bool interactive) {
        // One-liner commands (scripts) must never stop and ask questions, so only the
        // interactive shell gets the first-run setup.
        if (!interactive || ctx.app.settings().setupDone()) return;

        auto& io = ctx.io;
        io.out() << "Welcome to Pesa. This looks like your first time here.\n\n";
        if (runGuarded(ctx, {"setup"}) != 0) io.out() << "Run 'setup' whenever you are ready.\n";
        io.out() << "\n";
    }

} // namespace cli