// FTracker/cli/startup.cpp
// What happens every time Pesa opens, and the 'job' command that posts on demand.
#include <cli/commands.h>

#include <cli/display.h>

#include <ostream>

namespace cli {

namespace {

// Says what the job did. Quiet when there was nothing to say, unless `verbose`.
void report(Context& ctx, const pesa::DailyJobResult& r, bool verbose, std::ostream& out) {
    if (!r.problem.empty()) {
        ctx.io.err() << "Daily posting stopped: " << r.problem << ". Pesa will try again next time.\n";
        return;
    }
    if (r.clockBehind) {
        const auto last = ctx.app.dailyJob().lastPosted();
        out << "This computer's date is earlier than the last day Pesa posted"
            << (last ? " (" + last->toString() + ")" : "")
            << ". Daily posting is paused so entries do not land on the wrong days.\n";
        return;
    }
    if (r.entries > 0) {
        out << "Posted " << r.entries << (r.entries == 1 ? " entry" : " entries") << " (KES "
            << formatMoney(r.total) << ") up to " << r.to->toString() << ".\n";
    } else if (verbose) {
        out << (r.ran ? "Nothing new to post.\n" : "Auto-post is off.\n");
    }
}

} // namespace

void runStartup(Context& ctx, bool interactive) {
    auto& io = ctx.io;

    // One-liner commands (scripts) must never stop and ask questions, so only the
    // interactive shell gets the first-run setup.
    if (interactive && !ctx.app.settings().setupDone()) {
        io.out() << "Welcome to Pesa. This looks like your first time here.\n\n";
        if (runGuarded(ctx, {"setup"}) != 0) io.out() << "Run 'setup' whenever you are ready.\n";
        io.out() << "\n";
    }

    // In a script the messages go to the error stream, so they never mix into its output.
    std::ostream& out = interactive ? io.out() : io.err();
    try {
        const auto today = pesa::Date::today();
        if (ctx.app.settings().setupDone()) ctx.app.periods().ensureOpen(today);   // older files have no period yet

        // 1. Post the days that are due. 2. Only then close periods whose payday passed,
        //    so the old period's entries exist before its leftover is counted.
        report(ctx, ctx.app.dailyJob().run(today, pesa::TimeOfDay::now()), false, out);
        for (const auto& paid : ctx.app.periods().autoPayIfDue(today)) {
            out << "Payday arrived.\n";
            printPaid(out, paid);
        }
    } catch (const pesa::Error& e) {
        io.err() << "Error: " << e.what() << "\n";
    }
}

int runJob(Context& ctx, const Args& args) {
    if (args.size() > 1) { printUsage(ctx.io); return 2; }
    report(ctx, ctx.app.dailyJob().run(pesa::Date::today(), pesa::TimeOfDay::now()), true, ctx.io.out());
    ctx.io.out() << "Today's routine posts after " << ctx.app.settings().postAfter().toString()
                 << " ('settings postafter' changes it).\n";
    return 0;
}

} // namespace cli