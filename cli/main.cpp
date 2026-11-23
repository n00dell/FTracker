// FTracker/cli/main.cpp
#include "cli.h"

#include <iostream>
#include <optional>

int main(int argc, char** argv) {
    cli::Args args(argv + 1, argv + argc);

    try {
        std::optional<std::filesystem::path> dbPath;
        if (const auto db = cli::takeOption(args, "--db")) dbPath = *db;   // allowed anywhere
        if (args.empty()) { cli::printUsage(); return 2; }

        pesa::Pesa app(dbPath ? *dbPath : pesa::defaultDatabasePath());

        if (args[0] == "category") return cli::runCategory(app, args);
        if (args[0] == "item")     return cli::runItem(app, args);
        if (args[0] == "entry")    return cli::runEntry(app, args);
        if (args[0] == "day")      return cli::runDay(app, args);

        cli::printUsage();
        return 2;
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }
}
