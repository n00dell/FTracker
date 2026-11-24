// FTracker/cli/setup_cmd.cpp
// First-run setup, and the small app-wide settings.
#include <cli/commands.h>

namespace cli {

namespace {

void showSettings(Context& ctx) {
    const auto& settings = ctx.app.settings();
    auto& out = ctx.io.out();

    out << "  Setup done:     " << (settings.setupDone() ? "yes" : "no (run 'setup')") << "\n";
    if (const auto pay = settings.expectedPay()) {
        out << "  Expected pay:   " << formatMoney(*pay);
        if (const auto payday = settings.expectedPayday()) out << ", next payday about " << payday->toString();
        out << "\n";
    } else {
        out << "  Expected pay:   not set (projections stay off until you set one)\n";
    }
    out << "  Auto-post:      " << (settings.autoPostEnabled() ? "on" : "off")
        << " (runs when Pesa opens)\n";
}

} // namespace

int runSetup(Context& ctx, const Args& args) {
    if (args.size() > 1) { printUsage(ctx.io); return 2; }
    auto& io = ctx.io;
    auto& settings = ctx.app.settings();

    io.out() << "Pesa setup. Nothing is saved until the end (Ctrl-D cancels).\n\n";

    // The first question is the pay itself. Blank = "my income varies": spending is still
    // tracked, and projections switch on once you set a pay.
    const auto pay = io.askOptionalAmount("How much do you expect to be paid (KES)? Leave blank if it varies");

    std::optional<pesa::Date> payday;
    if (pay) {
        payday = io.askDate("Roughly when is your next payday",
                            settings.expectedPayday().value_or(pesa::Date::today().addDays(30)));
    }
    settings.completeSetup(pay, payday);

    io.out() << "\nSaved.\n";
    if (!pay) io.out() << "Projections stay off until you set a pay. Spending is still tracked.\n";
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
        bool on = settings.autoPostEnabled();
        if (const auto text = reader.next()) {
            const std::string answer = toLower(*text);
            if (answer == "on" || answer == "yes")       on = true;
            else if (answer == "off" || answer == "no")  on = false;
            else throw pesa::Error("Use 'on' or 'off'.");
        } else {
            on = io.askYesNo("Post routine entries automatically when Pesa opens?", on);
        }
        settings.setAutoPostEnabled(on);
        io.out() << "Auto-post is " << (on ? "on" : "off") << ".\n";
        return 0;
    }
    printUsage(io);
    return 2;
}

} // namespace cli