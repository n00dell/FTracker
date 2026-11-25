// FTracker/cli/setup_cmd.cpp
// First-run setup, and the small app-wide settings.
#include <cli/commands.h>

namespace cli {

namespace {

void showSettings(Context& ctx) {
    const auto& settings = ctx.app.settings();
    auto& out = ctx.io.out();
    const bool regular = settings.incomeMode() == pesa::IncomeMode::Regular;

    out << "  Setup done:       " << (settings.setupDone() ? "yes" : "no (run 'setup')") << "\n";
    out << "  Income:           " << (regular ? "regular" : "irregular (no payday to track)") << "\n";
    if (regular) {
        if (const auto pay = settings.expectedPay()) {
            out << "  Expected pay:     " << formatMoney(*pay);
            const auto period = ctx.app.periods().current();
            if (period && period->expectedPayday) out << ", next payday about " << period->expectedPayday->toString();
            out << "\n";
        }
        out << "  Auto-pay:         " << (settings.autoPayEnabled() ? "on" : "off")
            << " (closes the period when the payday arrives)\n";
    }
    out << "  Auto-post:        " << (settings.autoPostEnabled() ? "on" : "off") << " (runs when Pesa opens)\n";
    out << "  Post today after: " << settings.postAfter().toString() << " (earlier days always post)\n";
}

// "on"/"off" typed, or asked.
bool onOff(Console& io, ArgReader& reader, bool current, const std::string& question) {
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

    io.out() << "Pesa setup. Nothing is saved until the end (Ctrl-D cancels).\n\n";

    pesa::SetupAnswers answers;
    const bool wasIrregular = app.settings().incomeMode() == pesa::IncomeMode::Irregular;
    const auto kind = io.askChoice("How do you get paid",
        {"Regularly, like a salary (a set pay on a usual date)",
         "Irregularly, like freelance or project work"},
        wasIrregular && app.settings().setupDone() ? 1 : 0);
    answers.mode = kind == 0 ? pesa::IncomeMode::Regular : pesa::IncomeMode::Irregular;

    if (answers.mode == pesa::IncomeMode::Regular) {
        answers.pay = io.askAmount("How much is your pay (KES)", app.settings().expectedPay());
        const auto period = app.periods().current();
        const pesa::Date suggestion = (period && period->expectedPayday && *period->expectedPayday > today)
                                          ? *period->expectedPayday : today.addDays(30);
        answers.payday = io.askDate("Roughly when is your next payday", suggestion);
    }
    if (!app.periods().current()) {
        answers.startingBalance =
            io.askOptionalAmount("How much do you have right now (KES)? It becomes your first earned money")
              .value_or(0);
    }
    app.completeSetup(answers, today);

    io.out() << "\nSaved.\n";
    if (answers.mode == pesa::IncomeMode::Irregular) {
        io.out() << "No payday to track. Record each payment with 'income add', and press 'paid' when you\n"
                    "want to close a period and move what is left into savings.\n";
    }
    io.out() << "Next: add what you spend on ('catalog add'), then say when it posts ('rule add').\n";
    return 0;
}

int runSettings(Context& ctx, const Args& args) {
    auto& io = ctx.io;
    auto& settings = ctx.app.settings();

    const std::string sub = args.size() >= 2 ? args[1] : "show";
    ArgReader reader(args, 2);

    if (sub == "show") {
        showSettings(ctx);
        return 0;
    }
    if (sub == "autopost") {
        const bool on = onOff(io, reader, settings.autoPostEnabled(), "Post routine entries automatically when Pesa opens?");
        settings.setAutoPostEnabled(on);
        io.out() << "Auto-post is " << (on ? "on" : "off") << ".\n";
        return 0;
    }
    if (sub == "autopay") {
        const bool on = onOff(io, reader, settings.autoPayEnabled(), "Close the period automatically when the payday arrives?");
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
    printUsage(io);
    return 2;
}

} // namespace cli