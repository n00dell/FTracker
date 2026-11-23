// FTracker/cli/category_cmd.cpp
#include "cli.h"

#include <iomanip>
#include <iostream>

namespace cli {

    int runCategory(pesa::Pesa& app, const Args& args) {
        if (args.size() < 2) { printUsage(); return 2; }
        const std::string& sub = args[1];

        if (sub == "add" && args.size() >= 3) {
            const auto id = app.categories().add(joinFrom(args, 2));
            std::cout << "Added category #" << id << "\n";
            return 0;
        }
        if (sub == "list") {
            Args rest = args;
            const auto show = takeVisibility(rest);
            for (const auto& c : app.categories().list(show)) {
                const char* status = c.isDeleted ? "(deleted)" : (c.isActive ? "" : "(inactive)");
                std::cout << std::setw(4) << c.id << "  " << std::left << std::setw(24) << c.name
                          << std::right << status << "\n";
            }
            return 0;
        }
        if (args.size() >= 3) {
            const auto id = parseInt(args[2]);
            if (!id) { std::cerr << "Not a valid id: " << args[2] << "\n"; return 2; }

            if (sub == "rename" && args.size() >= 4) {
                app.categories().rename(*id, joinFrom(args, 3));
                std::cout << "Renamed.\n";
                return 0;
            }
            if (sub == "deactivate") { app.categories().setActive(*id, false); std::cout << "Deactivated.\n"; return 0; }
            if (sub == "activate")   { app.categories().setActive(*id, true);  std::cout << "Activated.\n";   return 0; }
            if (sub == "delete")     { app.categories().remove(*id);           std::cout << "Deleted.\n";     return 0; }
            if (sub == "restore")    { app.categories().restore(*id);          std::cout << "Restored.\n";    return 0; }
        }
        printUsage();
        return 2;
    }

} // namespace cli
