// FTracker/cli/main.cpp
#include <cli/commands.h>

#include <iostream>
#include <optional>

int main(int argc, char** argv) {
    cli::Args args(argv + 1, argv + argc);

    try {
        std::optional<std::filesystem::path> dbPath;
        if (const auto db = cli::takeOption(args, "--db")) dbPath = *db;   // allowed anywhere

        pesa::Pesa app(dbPath ? *dbPath : pesa::defaultDatabasePath());
        cli::Console io(std::cin, std::cout, std::cerr, cli::stdoutSupportsColor());
        cli::Context ctx{app, io};

        cli::runStartup(ctx, args.empty());   // on every launch, before anything else

        if (args.empty()) return cli::runShell(ctx);   // no command: the interactive shell
        return cli::runGuarded(ctx, args);
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }
}