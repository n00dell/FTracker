//
// Created by lee on 11/27/26.
//
// FTracker/cli/menu.cpp
// The main menu: pick an area, answer its questions, and q to go back. q at the menu leaves it.
#include <cli/commands.h>

namespace cli {

int runMenu(Context& ctx, const Args& args) {
    if (args.size() > 1) { printUsage(ctx.io); return 2; }
    auto& io = ctx.io;

    struct MenuItem { std::string label; Args command; };
    const std::vector<MenuItem> menu = {
        {"See today",                         {"day"}},
        {"Add or change an entry",            {"entry"}},
        {"Where I stand (status)",            {"status"}},
        {"I got paid",                        {"paid"}},
        {"Income: what arrived or is due",    {"income"}},
        {"Savings: pots and balances",        {"savings"}},
        {"Savings: add money",                {"savings", "add"}},
        {"Savings: new pot",                  {"savings", "newpot"}},
        {"Savings: move to earned",           {"savings", "move"}},
        {"Goals",                             {"goal"}},
        {"Funds",                             {"fund"}},
        {"Catalog: see it",                   {"catalog"}},
        {"Catalog: add something",            {"catalog", "add"}},
        {"Catalog: change something",         {"catalog", "edit"}},
        {"Rules",                             {"rule"}},
        {"Skips",                             {"skip"}},
        {"Log",                               {"log"}},
        {"Settings",                          {"settings"}},
    };
    std::vector<std::string> labels;
    for (const MenuItem& item : menu) labels.push_back(item.label);

    while (true) {
        std::size_t pick = 0;
        try {
            pick = io.askChoice("Main menu (q to leave)", labels);
        } catch (const Cancelled&) {
            return 0;
        } catch (const GoBack&) {
            return 0;
        }
        try {
            dispatch(ctx, menu[pick].command);
        } catch (const Cancelled&) {        // q inside an area: back to this menu, quietly
        } catch (const GoBack&) {
        } catch (const std::exception& e) {
            io.err() << "Error: " << e.what() << "\n";
        }
        io.out() << "\n";
    }
}

} // namespace cli