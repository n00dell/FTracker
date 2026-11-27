//
// Created by lee on 11/26/26.
//
// FTracker/core/src/goals.cpp
#include <pesa/goals.h>

#include <pesa/periods.h>
#include <pesa/savings.h>

#include "db.h"
#include "log_sql.h"
#include "money_sql.h"
#include "util.h"

#include <algorithm>

namespace pesa {

namespace {

    constexpr const char* kSelectGoal =
        "SELECT id, name, amount, priority, due_date, funded_from, donated, auto_buy, bought_on, bought_how, "
        "is_estimate, paid_amount FROM goals ";

std::string noGoal(GoalId id) { return "No goal with id " + std::to_string(id) + "."; }

Goal readGoal(const detail::Statement& st) {
    Goal g;
    g.id         = st.integer(0);
    g.name       = st.text(1);
    g.amount     = st.integer(2);
    g.priority   = static_cast<int>(st.integer(3));
    if (!st.isNull(4)) g.dueDate = Date::parse(st.text(4));
    g.fundedFrom = static_cast<FundedFrom>(st.integer(5));
    g.donated    = st.integer(6) != 0;
    g.autoBuy    = st.integer(7) != 0;
    g.isEstimate = st.integer(10) != 0;
    if (!st.isNull(11)) g.paidAmount = st.integer(11);
    if (!st.isNull(8)) g.boughtOn = Date::parse(st.text(8));
    if (!st.isNull(9)) g.boughtHow = static_cast<BuyMethod>(st.integer(9));
    return g;
}

// Names only have to be unique among goals that are NOT deleted.
bool nameTaken(detail::Database& db, const std::string& name, GoalId exceptId) {
    auto st = db.prepare("SELECT 1 FROM goals WHERE name = ? AND id != ? AND is_deleted = 0");
    st.bind(1, name).bind(2, exceptId);
    return st.step();
}

void validate(const Goal& g) {
    detail::requirePositive(g.amount);
    if (g.priority < 1) throw Error("The priority must be 1 or more (lower comes first).");
    if (g.autoBuy && !g.dueDate) throw Error("A goal that buys itself needs a date to buy on.");
}

} // namespace

// ---------------- purchase goals ----------------

GoalId Goals::add(const NewGoal& goal) {
    Goal g;
    g.name       = detail::cleanName(goal.name, "A goal needs a name.");
    g.amount     = goal.amount;
    g.priority   = goal.priority;
    g.dueDate    = goal.dueDate;
    g.fundedFrom = goal.fundedFrom;
    g.donated    = goal.donated;
    g.autoBuy    = goal.autoBuy;
    g.isEstimate = goal.isEstimate;
    validate(g);

    detail::Transaction tx(db_);
    if (nameTaken(db_, g.name, 0)) throw Error("A goal named '" + g.name + "' already exists.");

    auto insert = db_.prepare("INSERT INTO goals(name, amount, priority, due_date, funded_from, donated, auto_buy, is_estimate) "
                              "VALUES (?, ?, ?, ?, ?, ?, ?, ?)");
    insert.bind(1, g.name)
          .bind(2, g.amount)
          .bind(3, static_cast<std::int64_t>(g.priority))
          .bind(5, static_cast<std::int64_t>(g.fundedFrom))
          .bind(6, g.donated ? 1 : 0)
          .bind(7, g.autoBuy ? 1 : 0)
    .bind(8, g.isEstimate ? 1 : 0);insert.bind(1, g.name)
    .bind(2, g.amount)
    .bind(3, static_cast<std::int64_t>(g.priority))
    .bind(5, static_cast<std::int64_t>(g.fundedFrom))
    .bind(6, g.donated ? 1 : 0)
    .bind(7, g.autoBuy ? 1 : 0)
    .bind(8, g.isEstimate ? 1 : 0);
    if (g.dueDate) insert.bind(4, g.dueDate->toString()); else insert.bindNull(4);
    insert.run();
    const GoalId id = db_.lastInsertId();
    tx.commit();
    return id;
}

void Goals::edit(GoalId id, const GoalEdit& changes) {
    detail::Transaction tx(db_);
    auto goal = find(id);
    if (!goal) throw Error(noGoal(id));
    if (goal->isBought()) throw Error("'" + goal->name + "' is already bought, so it cannot be changed.");

    if (changes.name)       goal->name = detail::cleanName(*changes.name, "A goal needs a name.");
    if (changes.amount)     goal->amount = *changes.amount;
    if (changes.priority)   goal->priority = *changes.priority;
    if (changes.clearDate)  goal->dueDate.reset();
    else if (changes.dueDate) goal->dueDate = changes.dueDate;
    if (changes.fundedFrom) goal->fundedFrom = *changes.fundedFrom;
    if (changes.donated)    goal->donated = *changes.donated;
    if (changes.autoBuy)    goal->autoBuy = *changes.autoBuy;
    if (changes.isEstimate) goal->isEstimate = *changes.isEstimate;
    validate(*goal);
    if (nameTaken(db_, goal->name, id)) throw Error("A goal named '" + goal->name + "' already exists.");

    auto update = db_.prepare("UPDATE goals SET name = ?, amount = ?, priority = ?, due_date = ?, "
                              "funded_from = ?, donated = ?, auto_buy = ?, is_estimate = ? WHERE id = ?");
    update.bind(1, goal->name)
          .bind(2, goal->amount)
          .bind(3, static_cast<std::int64_t>(goal->priority))
          .bind(5, static_cast<std::int64_t>(goal->fundedFrom))
          .bind(6, goal->donated ? 1 : 0)
          .bind(7, goal->autoBuy ? 1 : 0)
          .bind(8, goal->isEstimate ? 1 : 0)
          .bind(9, id);
    if (goal->dueDate) update.bind(4, goal->dueDate->toString()); else update.bindNull(4);
    update.run();
    tx.commit();
}

void Goals::remove(GoalId id) {
    db_.prepare("UPDATE goals SET is_deleted = 1 WHERE id = ? AND is_deleted = 0").bind(1, id).run();
    if (db_.changes() == 0) throw Error(noGoal(id));
}

std::optional<Goal> Goals::find(GoalId id) const {
    auto st = db_.prepare(std::string(kSelectGoal) + "WHERE id = ? AND is_deleted = 0");
    st.bind(1, id);
    if (!st.step()) return std::nullopt;
    return readGoal(st);
}

std::vector<Goal> Goals::list(bool bought) const {
    std::string sql = std::string(kSelectGoal) + "WHERE is_deleted = 0 AND bought_on IS " +
                      (bought ? "NOT NULL ORDER BY bought_on DESC, id DESC" : "NULL ORDER BY priority, id");
    auto st = db_.prepare(sql);
    std::vector<Goal> out;
    while (st.step()) out.push_back(readGoal(st));
    return out;
}

std::vector<GoalStatus> Goals::statuses(const Date& today) const {
    const Savings savings(db_);
    const Money savingsAvailable = savings.findPot(savings.mainPot())->balance;

    Money earnedAvailable = 0;
    const Periods periods(db_, settings_);
    if (periods.current()) earnedAvailable = periods.summary(today).earnedRemaining();

    // Running totals of what the goals above need, kept separately for each place the money comes from.
    Money savingsNeeded = 0;
    Money earnedNeeded = 0;
    std::vector<GoalStatus> out;
    for (Goal& goal : list(false)) {   // already in priority order
        GoalStatus status;
        if (goal.donated) {
            status.covered = true;     // a gift takes no money
        } else {
            const bool fromSavings = goal.fundedFrom == FundedFrom::Savings;
            Money& needed = fromSavings ? savingsNeeded : earnedNeeded;
            needed += goal.amount;
            status.shortBy = std::max<Money>(0, needed - (fromSavings ? savingsAvailable : earnedAvailable));
            status.covered = status.shortBy == 0;
        }
        status.goal = std::move(goal);
        out.push_back(std::move(status));
    }
    return out;
}

void Goals::buy(GoalId id, BuyMethod method, const Date& date, std::optional<PotId> pot,
                std::optional<Money> actualAmount) {
    if (actualAmount) detail::requirePositive(*actualAmount);

    detail::Transaction tx(db_);
    const auto goal = find(id);
    if (!goal) throw Error(noGoal(id));
    if (goal->isBought()) throw Error("'" + goal->name + "' is already marked as bought.");

    // What actually leaves your money: the real price if you gave one, else the goal's amount.
    const Money price = actualAmount.value_or(goal->amount);

    Savings savings(db_);
    Entries entries(db_);
    const PotId source = pot.value_or(savings.mainPot());

    std::optional<BuyMethod> how;
    if (!goal->donated) {
        how = method;
        switch (method) {
            case BuyMethod::FromSavings: {
                const std::string potName = detail::requirePot(db_, source);
                detail::insertLedger(db_, date, source, -price, LedgerKind::GoalPurchase, goal->name);
                detail::writeLog(db_, LogKind::Savings, "Bought " + goal->name + ": KES " +
                                                            formatMoney(price) + " paid from " + potName);
                break;
            }
            case BuyMethod::MoveToEarned:
                savings.moveToEarned(date, source, price);
                entries.addOneOff(date, goal->name, price);
                break;
            case BuyMethod::FromEarned:
                entries.addOneOff(date, goal->name, price);
                break;
        }
    }

    auto mark = db_.prepare("UPDATE goals SET bought_on = ?, bought_how = ?, paid_amount = ? WHERE id = ?");
    mark.bind(1, date.toString()).bind(4, id);
    if (how) mark.bind(2, static_cast<std::int64_t>(*how)); else mark.bindNull(2);
    if (how) mark.bind(3, price);                 else mark.bindNull(3);   // a gift paid nothing
    mark.run();

    detail::writeLog(db_, LogKind::Manual,
                     "Bought goal " + goal->name + (goal->donated ? " (a gift: nothing deducted)"
                                                                  : ": KES " + formatMoney(price)));
    tx.commit();
}

GoalsDue Goals::processDue(const Date& today) {
    GoalsDue out;
    std::vector<Goal> dueGoals;
    for (Goal& goal : list(false)) {
        if (goal.dueDate && *goal.dueDate <= today) dueGoals.push_back(std::move(goal));
    }

    const Periods periods(db_, settings_);
    const auto period = periods.current();
    for (Goal& goal : dueGoals) {
        if (!goal.autoBuy || goal.isEstimate) {      // an estimate needs its real price from you
            out.waiting.push_back(std::move(goal));
            continue;
        }
        // Buy on the goal's date, but never before the current period began: an earned purchase
        // has to land in the period that pays for it.
        Date when = *goal.dueDate;
        const bool fromEarned = goal.fundedFrom == FundedFrom::Earned;
        if (fromEarned && period && when < period->start) when = period->start;
        try {
            buy(goal.id, goal.fundedFrom == FundedFrom::Savings ? BuyMethod::FromSavings : BuyMethod::FromEarned, when);
            out.bought.push_back(std::move(goal));
        } catch (const Error& e) {
            out.problems.push_back("Could not buy " + goal.name + ": " + e.what());
        }
    }
    return out;
}

// ---------------- fund goals ----------------

FundId Goals::addFund(std::string_view rawName, Money target, PotId pot, std::optional<Date> targetDate) {
    const std::string name = detail::cleanName(rawName, "A fund needs a name.");
    detail::requirePositive(target);

    detail::Transaction tx(db_);
    detail::requirePot(db_, pot);
    {
        auto st = db_.prepare("SELECT 1 FROM funds WHERE name = ? AND is_deleted = 0");
        st.bind(1, name);
        if (st.step()) throw Error("A fund named '" + name + "' already exists.");
    }
    auto insert = db_.prepare("INSERT INTO funds(name, target, pot_id, target_date) VALUES (?, ?, ?, ?)");
    insert.bind(1, name).bind(2, target).bind(3, pot);
    if (targetDate) insert.bind(4, targetDate->toString()); else insert.bindNull(4);
    insert.run();
    const FundId id = db_.lastInsertId();
    tx.commit();
    return id;
}

void Goals::setFundDate(FundId id, std::optional<Date> targetDate) {
    auto st = db_.prepare("UPDATE funds SET target_date = ? WHERE id = ? AND is_deleted = 0");
    if (targetDate) st.bind(1, targetDate->toString()); else st.bindNull(1);
    st.bind(2, id);
    st.run();
    if (db_.changes() == 0) throw Error("No fund with id " + std::to_string(id) + ".");
}

std::vector<Fund> Goals::funds() const {
    auto st = db_.prepare(
        "SELECT f.id, f.name, f.target, f.pot_id, p.name, "
        "COALESCE((SELECT SUM(l.amount) FROM ledger l WHERE l.pot_id = f.pot_id), 0), f.target_date "
        "FROM funds f JOIN pots p ON p.id = f.pot_id WHERE f.is_deleted = 0 ORDER BY f.id");
    std::vector<Fund> out;
    while (st.step()) {
        Fund fund{st.integer(0), st.text(1), st.integer(2), st.integer(3), st.text(4), st.integer(5)};
        if (!st.isNull(6)) fund.targetDate = Date::parse(st.text(6));
        out.push_back(std::move(fund));
    }
    return out;
}

FundProgress fundProgress(const Fund& fund, const Date& today) {
    FundProgress p;
    p.reached = fund.balance >= fund.target;
    if (!fund.targetDate) return p;

    p.daysLeft = today.daysUntil(*fund.targetDate);
    p.overdue = !p.reached && p.daysLeft < 0;
    if (!p.reached && p.daysLeft >= 0) {
        const int months = std::max(1, (p.daysLeft + 15) / 30);   // the nearest whole number of months, at least 1
        const Money remaining = fund.target - fund.balance;
        p.neededPerMonth = (remaining + months - 1) / months;      // rounded up
    }
    return p;
}

void Goals::removeFund(FundId id) {
    db_.prepare("UPDATE funds SET is_deleted = 1 WHERE id = ? AND is_deleted = 0").bind(1, id).run();
    if (db_.changes() == 0) throw Error("No fund with id " + std::to_string(id) + ".");
}


} // namespace pesa