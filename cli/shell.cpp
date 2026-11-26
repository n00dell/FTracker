// FTracker/cli/shell.cpp
#include <cli/commands.h>

namespace cli {
    int runShell(Context &ctx) {
        auto &io = ctx.io;
        io.out() << "Pesa shell. Type 'help' for the commands, 'exit' to leave.\n";

        while (true) {
            const auto line = io.readLine("pesa> ");
            if (!line) {
                io.out() << "\n";
                break;
            } // Ctrl-D

            Args args;
            try {
                args = splitLine(*line);
            } catch (const pesa::Error &e) {
                io.err() << "Error: " << e.what() << "\n";
                continue;
            }
            if (args.empty()) continue;
            if (args[0] == "exit" || args[0] == "quit") break;

            runGuarded(ctx, args); // errors are shown, the shell carries on
        }
        return 0;
    }
} // namespace cli