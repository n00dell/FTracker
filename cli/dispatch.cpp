// FTracker/cli/dispatch.cpp
#include <cli/commands.h>

namespace cli {
    void printUsage(Console &io) {
        io.out() <<
                "Pesa command line\n\n"
                "Run pesa-cli with no arguments for the interactive shell. Leave arguments out of\n"
                "any command and Pesa asks for them step by step (Enter on a blank question cancels).\n\n"
                "Catalog  (categories and the items inside them)\n"
                "  catalog [--all]                        show everything\n"
                "  catalog add                            add a category or an item, step by step\n"
                "  catalog edit                           pick something, then change it\n\n"
                "Categories\n"
                "  category add <name>\n"
                "  category list [--all | --deleted]\n"
                "  category rename <id> <new name>\n"
                "  category deactivate <id>               pause it (name stays reserved)\n"
                "  category activate <id>\n"
                "  category delete <id>                   soft delete (name becomes free)\n"
                "  category restore <id>                  undo a delete\n\n"
                "Items  (<category> is an id or a name; the built-in category Misc holds the leftovers)\n"
                "  item add <category> <amount> <name>\n"
                "  item list [<category>] [--all | --deleted]\n"
                "  item rename <id> <new name>\n"
                "  item amount <id> <amount>\n"
                "  item deactivate <id> | activate <id> | delete <id> | restore <id>\n\n"
                "Rules  (WHEN items post; the items say what and how much)\n"
                "  rule add <name> --days mon,tue,.. | daily | weekdays | weekend  [--monthly <day 1-31>]\n"
                "           [--item <item-id>]... [--from <date>] [--to <date>]\n"
                "  rule list [--all | --deleted]\n"
                "  rule preview <id> [<days>]             the dates it will post in the next days (default 30)\n"
                "  rule rename <id> <new name>\n"
                "  rule additem <id> <item-id> | rule removeitem <id> <item-id>\n"
                "  rule end <id> <last-day>               a change = end the old rule, add a new one\n"
                "  rule deactivate <id> | activate <id> | delete <id> | restore <id>\n\n"
                "Money  (periods, income, savings)\n"
                "  status                                 where you stand this period\n"
                "  paid [--date <d>] [--amount <n>] [--next <d>] [--pot <pot>]\n"
                "                                         I got paid: closes the period, the leftover goes to savings\n"
                "  delay [<days> | <date>]                my pay is late: move the expected payday\n"
                "  income add <amount> <label>            money that arrived (a gift, a project payment)\n"
                "  income list\n"
                "  savings                                pots and balances\n"
                "  savings ledger                         every movement\n"
                "  savings add <amount> [--pot <pot>] [<note>]\n"
                "  savings move <amount> [--pot <pot>]    move savings into earned money\n"
                "  savings cover <entry-id>...            pay those entries from savings\n"
                "  savings newpot <name> [--locked | --tied]\n\n"
                "Setup and settings\n"
                "  setup                                  expected pay and payday (asked on the first run)\n"
                "  settings                               show them\n"
                "  settings autopost on|off               let Pesa post your routine entries when it opens\n"
                "  settings autopay on|off                close the period by itself when the payday arrives\n"
                "  settings postafter <HH:MM>             today's routine posts only after this time (default 20:00)\n"
                "  job                                    run the daily posting now (it also runs whenever Pesa opens)\n\n"
                "Days and entries  (<date> is today, tomorrow, yesterday or 2026-11-23)\n"
                "  day [<date>]                           show a day\n"
                "  entry add <date> <item-id> [<amount>] [--savings]\n"
                "  entry oneoff <date> <amount> <name> [--savings]   (not in the catalog, no category)\n"
                "  entry amount <id> <amount>\n"
                "  entry fund <id> earned|savings\n"
                "  entry delete <id>\n\n"
                "In the shell: help, exit. Names with spaces can be typed with or without \"quotes\".\n";
    }

    int dispatch(Context &ctx, const Args &args) {
        const std::string &command = args.at(0);
        if (command == "catalog") return runCatalog(ctx, args);
        if (command == "category") return runCategory(ctx, args);
        if (command == "item") return runItem(ctx, args);
        if (command == "entry") return runEntry(ctx, args);
        if (command == "day") return runDay(ctx, args);
        if (command == "rule") return runRule(ctx, args);
        if (command == "setup") return runSetup(ctx, args);
        if (command == "settings") return runSettings(ctx, args);
        if (command == "status") return runStatus(ctx, args);
        if (command == "paid") return runPaid(ctx, args);
        if (command == "delay") return runDelay(ctx, args);
        if (command == "income") return runIncome(ctx, args);
        if (command == "savings") return runSavings(ctx, args);
        if (command == "help" || command == "?" || command == "--help") {
            printUsage(ctx.io);
            return 0;
        }
        if (command == "job") return runJob(ctx, args);

        ctx.io.err() << "Unknown command '" << command << "'. Type 'help' to see the commands.\n";
        return 2;
    }

    int runGuarded(Context &ctx, const Args &args) {
        try {
            return dispatch(ctx, args);
        } catch (const Cancelled &) {
            // must come first: it is also an std::exception
            ctx.io.out() << "Cancelled.\n";
            return 1;
        } catch (const std::exception &e) {
            ctx.io.err() << "Error: " << e.what() << "\n";
            return 1;
        }
    }
} // namespace cli
