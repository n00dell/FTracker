// FTracker/cli/startup.cpp
// What happens every time Pesa opens, and the 'job' command that does the same on demand.
#include <cli/commands.h>

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

pesa::DailyJobResult runDailyJob(Context& ctx) {
    return ctx.app.dailyJob().run(pesa::Date::today(), pesa::TimeOfDay::now());
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

    // The daily job runs on every launch. In a script its messages go to the error
    // stream so they never mix into the output the script is reading.
    try {
        report(ctx, runDailyJob(ctx), false, interactive ? io.out() : io.err());
    } catch (const pesa::Error& e) {
        io.err() << "Error: " << e.what() << "\n";
    }
}

int runJob(Context& ctx, const Args& args) {
    if (args.size() > 1) { printUsage(ctx.io); return 2; }
    report(ctx, runDailyJob(ctx), true, ctx.io.out());
    ctx.io.out() << "Today's routine posts after " << ctx.app.settings().postAfter().toString()
                 << " ('settings postafter' changes it).\n";
    return 0;
}

} // namespace cli