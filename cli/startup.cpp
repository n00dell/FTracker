// FTracker/cli/startup.cpp
// What happens every time Pesa opens, and the 'job' command that posts on demand.
#include <cli/commands.h>

#include <cli/display.h>

#include <ostream>

namespace cli {

namespace {

std::string joined(const std::vector<std::string>& names) {
    std::string out;
    for (const std::string& name : names) out += (out.empty() ? "" : ", ") + name;
    return out;
}

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

void reportAutoCover(const pesa::AutoCoverResult& r, std::ostream& out) {
    if (r.entries == 0) return;
    out << "Earned ran out, so " << r.entries << (r.entries == 1 ? " entry" : " entries") << " (KES "
        << formatMoney(r.total) << ") " << (r.entries == 1 ? "was" : "were") << " paid from Main savings (auto-cover is on).\n";
    if (r.savingsAfter < 0) {
        out << "  Warning: Main savings is now below zero (" << formatMoney(r.savingsAfter)
            << "). 'income add' if money arrived, or 'settings autocover off' to stop doing this.\n";
    }
}

// Earned below zero is allowed, but it deserves a clear warning and a way out.
void warnIfBelowZero(Context& ctx, const pesa::Date& today, std::ostream& out) {
    if (ctx.app.settings().autoCoverEnabled() || !ctx.app.periods().current()) return;
    const auto s = ctx.app.periods().summary(today);
    if (s.earnedRemaining() >= 0) return;
    out << "Earned is KES " << formatMoney(-s.earnedRemaining()) << " below zero. That is allowed. Your options:\n"
        << "  'income add' (money arrived)   'savings cover <entry id>' (pay entries from savings)\n"
        << "  'settings autocover on' (do that automatically)   'settings shortfall' (what happens at payday)\n";
}

void reportGoals(Context& ctx, const pesa::GoalsDue& due, std::ostream& out) {
    for (const auto& g : due.bought) {
        out << "Bought goal " << g.name << " (KES " << formatMoney(g.amount) << ")"
            << (g.donated ? ", a gift: nothing deducted.\n" : " automatically.\n");
    }
    for (const auto& g : due.waiting) {
        out << "Goal due: " << g.name << " (KES " << formatMoney(g.amount) << ", " << g.dueDate->toString()
            << "). 'goal buy " << g.id << "' when you are ready.\n";
    }
    for (const auto& problem : due.problems) ctx.io.err() << problem << "\n";
}

// A fund whose date passed is not changed behind your back: you are told, and you decide.
void reportFunds(Context& ctx, const pesa::Date& today, std::ostream& out) {
    for (const auto& fund : ctx.app.goals().funds()) {
        if (!pesa::fundProgress(fund, today).overdue) continue;
        out << "Fund " << fund.name << " missed its date (" << fund.targetDate->toString() << "): "
            << formatMoney(fund.balance) << " of " << formatMoney(fund.target) << ". 'fund date " << fund.id
            << " <new date>' moves it, or 'fund delete " << fund.id << "' closes it.\n";
    }
}

} // namespace

void autoCoverAfterChange(Context& ctx) {
    if (!ctx.app.settings().setupDone() || !ctx.app.periods().current()) return;
    reportAutoCover(ctx.app.periods().autoCoverIfNeeded(pesa::Date::today()), ctx.io.out());
}

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
        const bool ready = ctx.app.settings().setupDone();
        if (ready) ctx.app.periods().ensureOpen(today);   // older files have no period yet

        // 1. Post the days that are due (the job respects end dates itself). 2. Only then close periods whose
        // payday passed, so the old period's entries exist before its leftover is counted. 3. Then the rest.
        report(ctx, ctx.app.dailyJob().run(today, pesa::TimeOfDay::now()), false, out);
        for (const auto& paid : ctx.app.periods().autoPayIfDue(today)) {
            out << "Payday arrived.\n";
            printPaid(out, paid);
        }

        auto paused = ctx.app.categories().pauseExpired(today);
        for (auto& name : ctx.app.items().pauseExpired(today)) paused.push_back(std::move(name));
        if (!paused.empty()) out << "Paused, because their last day passed: " << joined(paused) << ".\n";

        if (ready) {
            reportAutoCover(ctx.app.periods().autoCoverIfNeeded(today), out);
            warnIfBelowZero(ctx, today, out);
            reportGoals(ctx, ctx.app.goals().processDue(today), out);
            reportFunds(ctx, today, out);
            answerDueIncome(ctx, interactive);   // the "did it arrive?" questions (or a reminder line in a script)
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