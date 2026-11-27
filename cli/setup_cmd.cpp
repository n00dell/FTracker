// FTracker/cli/setup_cmd.cpp
// First-run setup, and the small app-wide settings.
#include <cli/commands.h>

namespace cli {
    namespace {
        void showSettings(Context &ctx) {
            const auto &settings = ctx.app.settings();
            auto &out = ctx.io.out();
            const bool regular = settings.incomeMode() == pesa::IncomeMode::Regular;

            out << "  Setup done:       " << (settings.setupDone() ? "yes" : "no (run 'setup')") << "\n";
            out << "  Income:           " << (regular ? "regular" : "irregular (no payday to track)") << "\n";
            if (regular) {
                if (const auto pay = settings.expectedPay()) {
                    out << "  Expected pay:     " << formatMoney(*pay);
                    const auto period = ctx.app.periods().current();
                    if (period && period->expectedPayday)
                        out << ", next payday about " << period->expectedPayday->toString();
                    out << "\n";
                }
                out << "  Auto-pay:         " << (settings.autoPayEnabled() ? "on" : "off")
                        << " (closes the period when the payday arrives)\n";
            }
            out << "  Auto-post:        " << (settings.autoPostEnabled() ? "on" : "off") << " (runs when Pesa opens)\n";
            out << "  Post today after: " << settings.postAfter().toString() << " (earlier days always post)\n";
            out << "  Auto-cover:       " << (settings.autoCoverEnabled() ? "on" : "off")
                    << " (pays from Main savings when earned runs out)\n";
            out << "  If overspent at payday: "
                    << (settings.shortfallPolicy() == pesa::ShortfallPolicy::TakeFromSavings
                            ? "take it from Main savings"
                            : settings.shortfallPolicy() == pesa::ShortfallPolicy::CarryForward
                                  ? "carry it into the new period"
                                  : "only record it") << "\n";
        }

        // "on"/"off" typed, or asked.
        bool onOff(Console &io, ArgReader &reader, bool current, const std::string &question) {
            if (const auto text = reader.next()) {
                const std::string answer = toLower(*text);
                if (answer == "on" || answer == "yes") return true;
                if (answer == "off" || answer == "no") return false;
                throw pesa::Error("Use 'on' or 'off'.");
            }
            return io.askYesNo(question, current);
        }
    } // namespace

    int runSetup(Context& ctx, const Args& args) {
    if (args.size() > 1) { printUsage(ctx.io); return 2; }
    auto& io = ctx.io;
    auto& app = ctx.app;
    const pesa::Date today = pesa::Date::today();

    io.out() << "Pesa setup. Nothing is saved until the end. q leaves, < goes back a question.\n\n";

    pesa::SetupAnswers answers;
    std::size_t kind = (app.settings().setupDone() && app.settings().incomeMode() == pesa::IncomeMode::Irregular) ? 1 : 0;
    const auto regular = [&] { return answers.mode == pesa::IncomeMode::Regular; };
    const pesa::Date suggestion = [&] {
        const auto period = app.periods().current();
        return (period && period->expectedPayday && *period->expectedPayday > today) ? *period->expectedPayday
                                                                                     : today.addDays(30);
    }();

    std::vector<Step> steps;
    steps.push_back({[&] {
        kind = io.askChoice("How do you get paid",
                            {"Regularly, like a salary (a set pay on a usual date)",
                             "Irregularly, like freelance or project work"}, kind);
        answers.mode = kind == 0 ? pesa::IncomeMode::Regular : pesa::IncomeMode::Irregular;
    }});
    steps.push_back({[&] { answers.pay = io.askAmount("How much is your pay (KES)", answers.pay ? answers.pay : app.settings().expectedPay()); },
                     regular});
    steps.push_back({[&] { answers.payday = io.askDate("Roughly when is your next payday", answers.payday ? answers.payday : std::optional(suggestion)); },
                     regular});
    steps.push_back({[&] {
        answers.startingBalance =
            io.askOptionalAmount("How much do you have right now (KES)? It becomes your first earned money").value_or(0);
    }, [&] { return !app.periods().current(); }});
    runSteps(steps);

    app.completeSetup(answers, today);

    io.out() << "\nSaved.\n";
    if (answers.mode == pesa::IncomeMode::Irregular) {
        io.out() << "No payday to track. Record each payment with 'income add', and press 'paid' when you\n"
                    "want to close a period and move what is left into savings.\n";
    }
    io.out() << "Next: add what you spend on ('catalog add'), then say when it posts ('rule add').\n";
    return 0;
}

    int runSettings(Context &ctx, const Args &args) {
        auto &io = ctx.io;
        auto &settings = ctx.app.settings();

        const std::string sub = args.size() >= 2 ? args[1] : "show";
        ArgReader reader(args, 2);

        if (sub == "show") {
            showSettings(ctx);
            return 0;
        }
        if (sub == "autopost") {
            const bool on = onOff(io, reader, settings.autoPostEnabled(),
                                  "Post routine entries automatically when Pesa opens?");
            settings.setAutoPostEnabled(on);
            io.out() << "Auto-post is " << (on ? "on" : "off") << ".\n";
            return 0;
        }
        if (sub == "autopay") {
            const bool on = onOff(io, reader, settings.autoPayEnabled(),
                                  "Close the period automatically when the payday arrives?");
            settings.setAutoPayEnabled(on);
            io.out() << "Auto-pay is " << (on ? "on" : "off") << ".\n";
            return 0;
        }
        if (sub == "postafter") {
            const auto text = reader.next();
            const auto time = pesa::TimeOfDay::parse(
                text ? *text : io.askText("Post today's routine after (HH:MM)", settings.postAfter().toString()));
            settings.setPostAfter(time);
            io.out() << "Today's routine entries post after " << time.toString() << ".\n";
            return 0;
        }
        if (sub == "autocover") {
            const bool on = onOff(io, reader, settings.autoCoverEnabled(),
                                  "When earned runs out, pay the newest spending from Main savings automatically?");
            settings.setAutoCoverEnabled(on);
            io.out() << "Auto-cover is " << (on ? "on" : "off") << ".\n";
            if (on) io.out() << "Nothing is ever blocked: Main savings can go below zero, and you are warned when it does.\n";
            return 0;
        }
        if (sub == "shortfall") {
            std::string word;
            if (const auto text = reader.next()) {
                word = toLower(*text);
            } else {
                const auto choice = io.askChoice("If earned ends below zero when you get paid",
                    {"Only record it", "Take it from Main savings", "Carry it into the new period as its first expense"},
                    static_cast<std::size_t>(settings.shortfallPolicy()));
                word = choice == 0 ? "record" : choice == 1 ? "savings" : "carry";
            }
            if      (word == "record")  settings.setShortfallPolicy(pesa::ShortfallPolicy::Record);
            else if (word == "savings") settings.setShortfallPolicy(pesa::ShortfallPolicy::TakeFromSavings);
            else if (word == "carry")   settings.setShortfallPolicy(pesa::ShortfallPolicy::CarryForward);
            else throw pesa::Error("Use record, savings or carry.");
            io.out() << "Saved.\n";
            return 0;
        }
        printUsage(io);
        return 2;
    }
} // namespace cli
