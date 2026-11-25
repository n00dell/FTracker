//
// Created by lee on 11/25/26.
//
// FTracker/core/src/periods.cpp
#include <pesa/periods.h>

#include <pesa/entries.h>
#include "log_sql.h"
#include "db.h"
#include "money_sql.h"
#include "util.h"

namespace pesa {

namespace {

constexpr const char* kSelectPeriod =
    "SELECT id, start_date, end_date, expected_payday, remainder FROM periods ";

std::optional<Date> optionalDate(const detail::Statement& st, int column) {
    if (st.isNull(column)) return std::nullopt;
    return Date::parse(st.text(column));
}

void bindOptionalDate(detail::Statement& st, int index, const std::optional<Date>& date) {
    if (date) st.bind(index, date->toString()); else st.bindNull(index);
}

Period readPeriod(const detail::Statement& st) {
    Period p;
    p.id             = st.integer(0);
    p.start          = Date::parse(st.text(1));
    p.end            = optionalDate(st, 2);
    p.expectedPayday = optionalDate(st, 3);
    if (!st.isNull(4)) p.remainder = st.integer(4);
    return p;
}

// Entries paid from `fund`, dated from `from` up to (not including) `before`. Skipped ones don't count.
Money spent(detail::Database& db, FundedFrom fund, const Date& from, const Date& before) {
    auto st = db.prepare("SELECT COALESCE(SUM(amount), 0) FROM entries "
                         "WHERE funded_from = ? AND kind != 3 AND date >= ? AND date < ?");
    st.bind(1, static_cast<std::int64_t>(fund)).bind(2, from.toString()).bind(3, before.toString());
    st.step();
    return st.integer(0);
}

Money incomeOf(detail::Database& db, PeriodId period) {
    auto st = db.prepare("SELECT COALESCE(SUM(amount), 0) FROM income WHERE period_id = ?");
    st.bind(1, period);
    st.step();
    return st.integer(0);
}

// Keeps the usual day of the month (the one that was expected), one month on. If pay came
// very late, keep stepping until the date is in the future.
Date nextPaydayAfter(const Period& closing, const Date& paidOn) {
    Date next = closing.expectedPayday.value_or(paidOn).addMonths(1);
    while (next <= paidOn) next = next.addMonths(1);
    return next;
}

} // namespace

Period Periods::requireCurrent() const {
    const auto period = current();
    if (!period) throw Error("There is no open period. Run 'setup' first.");
    return *period;
}

std::optional<Period> Periods::current() const {
    auto st = db_.prepare(std::string(kSelectPeriod) + "WHERE end_date IS NULL");
    if (!st.step()) return std::nullopt;
    return readPeriod(st);
}

std::vector<Period> Periods::history() const {
    auto st = db_.prepare(std::string(kSelectPeriod) + "WHERE end_date IS NOT NULL ORDER BY start_date DESC");
    std::vector<Period> out;
    while (st.step()) out.push_back(readPeriod(st));
    return out;
}

bool Periods::ensureOpen(const Date& today, Money startingBalance) {
    detail::Transaction tx(db_);
    if (current()) { tx.commit(); return false; }

    db_.prepare("INSERT INTO periods(start_date) VALUES (?)").bind(1, today.toString()).run();
    const PeriodId id = db_.lastInsertId();
    if (startingBalance > 0) detail::insertIncome(db_, id, today, "Starting balance", startingBalance);
    detail::writeLog(db_, LogKind::Pay, "First period opened on " + today.toString() +
                                            (startingBalance > 0 ? " with KES " + formatMoney(startingBalance) : ""));
    tx.commit();
    return true;
}

void Periods::setExpectedPayday(std::optional<Date> payday) {
    const Period period = requireCurrent();
    auto st = db_.prepare("UPDATE periods SET expected_payday = ? WHERE id = ?");
    bindOptionalDate(st, 1, payday);
    st.bind(2, period.id);
    st.run();
}

PeriodSummary Periods::summary(const Date& today) const {
    PeriodSummary s;
    s.period = requireCurrent();
    const Date before = today.nextDay();   // entries dated after today have not happened yet
    s.income           = incomeOf(db_, s.period.id);
    s.spentFromEarned  = spent(db_, FundedFrom::Earned, s.period.start, before);
    s.spentFromSavings = spent(db_, FundedFrom::Savings, s.period.start, before);
    if (settings_.incomeMode() == IncomeMode::Regular && s.period.expectedPayday &&
        today > *s.period.expectedPayday) {
        s.daysLate = s.period.expectedPayday->daysUntil(today);
    }
    return s;
}

    IncomeId Periods::addIncome(const Date& date, std::string_view rawLabel, Money amount) {
    const std::string label = detail::cleanName(rawLabel, "Say who the income is from.");
    detail::requirePositive(amount);
    const Period period = requireCurrent();

    detail::Transaction tx(db_);
    detail::insertIncome(db_, period.id, date, label, amount);
    const IncomeId id = db_.lastInsertId();   // moved up directly after insertIncome
    detail::writeLog(db_, LogKind::Pay, "Income: " + label + ", KES " + formatMoney(amount));
    tx.commit();

    return id;
}

std::vector<IncomeLine> Periods::income() const {
    const Period period = requireCurrent();
    auto st = db_.prepare("SELECT id, period_id, date, label, amount FROM income "
                          "WHERE period_id = ? ORDER BY date, id");
    st.bind(1, period.id);
    std::vector<IncomeLine> out;
    while (st.step()) {
        out.push_back(IncomeLine{st.integer(0), st.integer(1), Date::parse(st.text(2)),
                                 st.text(3), st.integer(4)});
    }
    return out;
}

Date Periods::estimateNextPayday(const Date& paidOn) const {
    return nextPaydayAfter(requireCurrent(), paidOn);
}

PaidResult Periods::markPaid(const PaidOptions& options) {
    detail::Transaction tx(db_);   // all of it, or none of it
    const Period closing = requireCurrent();
    if (options.paidOn <= closing.start) {
        throw Error("The current period started on " + closing.start.toString() +
                    ". Pick a later date for getting paid.");
    }

    const auto pay = options.amount ? options.amount : settings_.expectedPay();
    if (!pay) throw Error("Say how much you were paid.");
    detail::requirePositive(*pay);

    std::optional<Date> nextPayday;
    if (settings_.incomeMode() == IncomeMode::Regular) {
        nextPayday = options.nextPayday.value_or(nextPaydayAfter(closing, options.paidOn));
        if (*nextPayday <= options.paidOn) {
            throw Error("The next payday must be after " + options.paidOn.toString() + ".");
        }
    }

    // 1. The numbers of the period that is closing.
    const Money remainder = incomeOf(db_, closing.id) -
                            spent(db_, FundedFrom::Earned, closing.start, options.paidOn);

    // 2. Close it. (This comes before opening the next one: only one period may be open.)
    auto close = db_.prepare("UPDATE periods SET end_date = ?, remainder = ? WHERE id = ?");
    close.bind(1, options.paidOn.toString()).bind(2, remainder).bind(3, closing.id);
    close.run();

    PaidResult result;
    result.paidOn = options.paidOn;
    result.periodStart = closing.start;
    result.pay = *pay;
    result.remainder = remainder;
    result.nextPayday = nextPayday;

    // 3. A leftover goes straight into savings. A shortfall is only recorded.
    if (remainder > 0) {
        const PotId pot = options.leftoverPot.value_or(detail::mainPotId(db_));
        result.leftoverPot = detail::requirePot(db_, pot);
        detail::insertLedger(db_, options.paidOn, pot, remainder, LedgerKind::PeriodRemainder,
                             "Paid on " + options.paidOn.toString(), closing.id);
        result.movedToSavings = remainder;
    } else if (remainder < 0) {
        result.shortfall = -remainder;
    }

    // 4. The new period starts on the pay day, with the pay as its first income.
    auto open = db_.prepare("INSERT INTO periods(start_date, expected_payday) VALUES (?, ?)");
    open.bind(1, options.paidOn.toString());
    bindOptionalDate(open, 2, nextPayday);
    open.run();
    detail::insertIncome(db_, db_.lastInsertId(), options.paidOn, "Pay", *pay);
    detail::writeLog(db_, LogKind::Pay,
                     std::string(options.automatic ? "Payday arrived (auto-pay) on " : "Got paid on ") +
                         options.paidOn.toString() + ": KES " + formatMoney(*pay) + " added, new period started");
    if (result.movedToSavings > 0) {
        detail::writeLog(db_, LogKind::Savings, "Leftover KES " + formatMoney(result.movedToSavings) +
                                                    " moved to " + result.leftoverPot);
    } else if (result.shortfall > 0) {
        detail::writeLog(db_, LogKind::Pay, "Earned ran KES " + formatMoney(result.shortfall) +
                                                " over; nothing was taken from savings");
    }

    tx.commit();
    return result;
}

std::vector<PaidResult> Periods::autoPayIfDue(const Date& today) {
    std::vector<PaidResult> done;
    if (settings_.incomeMode() != IncomeMode::Regular || !settings_.autoPayEnabled() ||
        !settings_.expectedPay()) {
        return done;
    }

    detail::Transaction tx(db_);   // every missed payday, or none
    for (int guard = 0; guard < 24; ++guard) {   // two years of missed paydays at most
        const auto period = current();
        if (!period || !period->expectedPayday) break;
        const Date due = *period->expectedPayday;
        if (due > today || due <= period->start) break;
        done.push_back(markPaid({.paidOn = due, .automatic = true}));
    }
    tx.commit();
    return done;
}

void Periods::delayPayday(const Date& newPayday, const Date& today) {
    if (settings_.incomeMode() != IncomeMode::Regular) {
        throw Error("Your income is irregular, so there is no payday to delay.");
    }
    const Period period = requireCurrent();
    if (newPayday <= today) throw Error("Pick a payday after today. If the pay arrived, use 'paid'.");
    if (newPayday <= period.start) throw Error("The payday must be after " + period.start.toString() + ".");
    detail::Transaction tx(db_);
    setExpectedPayday(newPayday);
    detail::writeLog(db_, LogKind::Pay, "Salary delayed: expected payday moved from " +
                                            period.expectedPayday.value_or(newPayday).toString() + " to " +
                                            newPayday.toString());
    tx.commit();
}

} // namespace pesa