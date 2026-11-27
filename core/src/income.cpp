//
// Created by lee on 11/27/26.
//
// FTracker/core/src/income.cpp
#include <pesa/income.h>

#include "db.h"
#include "log_sql.h"
#include "money_sql.h"
#include "util.h"

#include <utility>

namespace pesa {

namespace {

constexpr const char* kSelectSource =
    "SELECT s.id, s.name, s.usual_amount, s.frequency, s.next_expected, s.pot_id, p.name, s.snoozed_on "
    "FROM income_sources s LEFT JOIN pots p ON p.id = s.pot_id ";

IncomeSource readSource(const detail::Statement& st) {
    IncomeSource s;
    s.id          = st.integer(0);
    s.name        = st.text(1);
    s.usualAmount = st.integer(2);
    s.frequency   = static_cast<Frequency>(st.integer(3));
    if (!st.isNull(4)) s.nextExpected = Date::parse(st.text(4));
    if (!st.isNull(5)) s.potId = st.integer(5);
    s.goesTo      = st.isNull(5) ? "Earned" : st.text(6);
    if (!st.isNull(7)) s.snoozedOn = Date::parse(st.text(7));
    return s;
}

int stepMonths(Frequency f) {
    switch (f) {
        case Frequency::Monthly:   return 1;
        case Frequency::Quarterly: return 3;
        case Frequency::Yearly:    return 12;
        case Frequency::OneOff:
        case Frequency::WithPay:   return 0;
    }
    return 0;
}

// The next date after a payment (or a skip): keeps the usual day of the month, and steps on
// until the date is after `today`, so late money never leaves the next date in the past.
Date nextAfter(const Date& expected, Frequency f, const Date& today) {
    const int step = stepMonths(f);
    if (step <= 0) throw Error("That income does not repeat.");
    Date next = expected.addMonths(step);
    while (next <= today) next = next.addMonths(step);
    return next;
}

PeriodId openPeriod(detail::Database& db) {
    auto st = db.prepare("SELECT id FROM periods WHERE end_date IS NULL");
    if (!st.step()) throw Error("There is no open period. Run 'setup' first.");
    return st.integer(0);
}

std::string noSource(SourceId id) { return "No expected income with id " + std::to_string(id) + "."; }

void bindOptionalDate(detail::Statement& st, int index, const std::optional<Date>& date) {
    if (date) st.bind(index, date->toString()); else st.bindNull(index);
}

} // namespace

std::string frequencyName(Frequency frequency) {
    switch (frequency) {
        case Frequency::OneOff:    return "One-off";
        case Frequency::WithPay:   return "With my pay";
        case Frequency::Monthly:   return "Monthly";
        case Frequency::Quarterly: return "Every 3 months";
        case Frequency::Yearly:    return "Yearly";
    }
    return "";
}

// ---------------- pay history ----------------

void Income::setPay(Money amount, const Date& from) {
    detail::requirePositive(amount);
    detail::Transaction tx(db_);

    std::optional<std::pair<std::int64_t, Date>> last;   // the newest row
    {
        auto st = db_.prepare("SELECT id, from_date FROM pay_history ORDER BY from_date DESC LIMIT 1");
        if (st.step()) last = std::pair{st.integer(0), Date::parse(st.text(1))};
    }
    if (last && from < last->second) {
        throw Error("The pay history already has an amount from " + last->second.toString() +
                    ". Pick that day or a later one.");
    }

    if (last && from == last->second) {
        // The same day: this is a correction, not a raise.
        db_.prepare("UPDATE pay_history SET amount = ? WHERE id = ?").bind(1, amount).bind(2, last->first).run();
    } else {
        if (last) {   // the old amount ends the day before the new one starts
            db_.prepare("UPDATE pay_history SET to_date = ? WHERE id = ?")
                .bind(1, from.previousDay().toString()).bind(2, last->first).run();
        }
        db_.prepare("INSERT INTO pay_history(amount, from_date) VALUES (?, ?)")
            .bind(1, amount).bind(2, from.toString()).run();
    }
    detail::writeLog(db_, LogKind::Pay, "Pay is KES " + formatMoney(amount) + " from " + from.toString());
    tx.commit();
}

std::optional<Money> Income::payOn(const Date& date) const {
    {
        auto st = db_.prepare("SELECT amount FROM pay_history WHERE from_date <= ?1 "
                              "AND (to_date IS NULL OR to_date >= ?1) ORDER BY from_date DESC LIMIT 1");
        st.bind(1, date.toString());
        if (st.step()) return st.integer(0);
    }
    {   // A day before the first row (getting paid early in the first period): the first amount.
        auto st = db_.prepare("SELECT amount FROM pay_history ORDER BY from_date LIMIT 1");
        if (st.step()) return st.integer(0);
    }
    return settings_.expectedPay();   // no history at all: whatever the settings hold
}

std::vector<PayRate> Income::payHistory() const {
    auto st = db_.prepare("SELECT amount, from_date, to_date FROM pay_history ORDER BY from_date");
    std::vector<PayRate> out;
    while (st.step()) {
        PayRate rate{st.integer(0), Date::parse(st.text(1)), std::nullopt};
        if (!st.isNull(2)) rate.to = Date::parse(st.text(2));
        out.push_back(rate);
    }
    return out;
}

// ---------------- other income ----------------

SourceId Income::addSource(const NewSource& source) {
    const std::string name = detail::cleanName(source.name, "Say who the income is from.");
    detail::requirePositive(source.usualAmount);
    if (source.frequency == Frequency::WithPay && source.nextExpected) {
        throw Error("Income that comes with your pay has no date of its own.");
    }

    detail::Transaction tx(db_);
    if (source.potId) detail::requirePot(db_, *source.potId);
    {
        auto st = db_.prepare("SELECT 1 FROM income_sources WHERE name = ? AND is_deleted = 0");
        st.bind(1, name);
        if (st.step()) throw Error("Expected income called '" + name + "' already exists.");
    }
    auto insert = db_.prepare("INSERT INTO income_sources(name, usual_amount, frequency, next_expected, pot_id) "
                              "VALUES (?, ?, ?, ?, ?)");
    insert.bind(1, name).bind(2, source.usualAmount).bind(3, static_cast<std::int64_t>(source.frequency));
    bindOptionalDate(insert, 4, source.nextExpected);
    if (source.potId) insert.bind(5, *source.potId); else insert.bindNull(5);
    insert.run();
    const SourceId id = db_.lastInsertId();

    detail::writeLog(db_, LogKind::Pay, "Expecting " + name + ": KES " + formatMoney(source.usualAmount) + ", " +
                                            frequencyName(source.frequency) +
                                            (source.nextExpected ? ", around " + source.nextExpected->toString() : ""));
    tx.commit();
    return id;
}

void Income::removeSource(SourceId id) {
    db_.prepare("UPDATE income_sources SET is_deleted = 1 WHERE id = ? AND is_deleted = 0").bind(1, id).run();
    if (db_.changes() == 0) throw Error(noSource(id));
}

std::optional<IncomeSource> Income::findSource(SourceId id) const {
    auto st = db_.prepare(std::string(kSelectSource) + "WHERE s.id = ? AND s.is_deleted = 0");
    st.bind(1, id);
    if (!st.step()) return std::nullopt;
    return readSource(st);
}

std::vector<IncomeSource> Income::sources() const {
    auto st = db_.prepare(std::string(kSelectSource) +
                          "WHERE s.is_deleted = 0 ORDER BY s.next_expected IS NULL, s.next_expected, s.id");
    std::vector<IncomeSource> out;
    while (st.step()) out.push_back(readSource(st));
    return out;
}

std::vector<IncomeSource> Income::due(const Date& today) const {
    auto st = db_.prepare(std::string(kSelectSource) +
                          "WHERE s.is_deleted = 0 AND s.frequency != 1 AND s.next_expected IS NOT NULL "
                          "AND s.next_expected <= ?1 AND (s.snoozed_on IS NULL OR s.snoozed_on < ?1) "
                          "ORDER BY s.next_expected, s.id");
    st.bind(1, today.toString());
    std::vector<IncomeSource> out;
    while (st.step()) out.push_back(readSource(st));
    return out;
}

SourceReceived Income::received(SourceId id, std::optional<Money> amount, const Date& today) {
    detail::Transaction tx(db_);
    const auto source = findSource(id);
    if (!source) throw Error(noSource(id));
    if (source->frequency == Frequency::WithPay) {
        throw Error("'" + source->name + "' is added automatically when you get paid.");
    }
    const Money value = amount.value_or(source->usualAmount);
    detail::requirePositive(value);

    // The money counts from now on: into this period's earned, or into its pot.
    if (source->potId) detail::insertLedger(db_, today, *source->potId, value, LedgerKind::AddedToSavings, source->name);
    else               detail::insertIncome(db_, openPeriod(db_), today, source->name, value);

    SourceReceived result{source->name, value, source->goesTo, std::nullopt, false};
    if (source->frequency == Frequency::OneOff) {
        db_.prepare("UPDATE income_sources SET is_deleted = 1 WHERE id = ?").bind(1, id).run();
        result.removed = true;
    } else if (source->nextExpected) {
        result.nextExpected = nextAfter(*source->nextExpected, source->frequency, today);
        db_.prepare("UPDATE income_sources SET next_expected = ?, snoozed_on = NULL WHERE id = ?")
            .bind(1, result.nextExpected->toString()).bind(2, id).run();
    }   // recurring with no date: it stays "no plan yet"

    detail::writeLog(db_, LogKind::Pay, "Received " + source->name + ": KES " + formatMoney(value) +
                                            (source->potId ? " into " + source->goesTo : ""));
    tx.commit();
    return result;
}

Date Income::skip(SourceId id, const Date& today) {
    detail::Transaction tx(db_);
    const auto source = findSource(id);
    if (!source) throw Error(noSource(id));
    if (source->frequency == Frequency::OneOff || source->frequency == Frequency::WithPay) {
        throw Error("Skipping is for income that repeats. Use 'remove' if you no longer expect it.");
    }
    if (!source->nextExpected) throw Error("Nothing is expected from '" + source->name + "', so there is nothing to skip.");

    const Date next = nextAfter(*source->nextExpected, source->frequency, today);
    db_.prepare("UPDATE income_sources SET next_expected = ?, snoozed_on = NULL WHERE id = ?")
        .bind(1, next.toString()).bind(2, id).run();
    detail::writeLog(db_, LogKind::Pay, "Skipped " + source->name + " this time; next expected " + next.toString());
    tx.commit();
    return next;
}

void Income::notYet(SourceId id, const Date& today) {
    db_.prepare("UPDATE income_sources SET snoozed_on = ? WHERE id = ? AND is_deleted = 0")
        .bind(1, today.toString()).bind(2, id).run();
    if (db_.changes() == 0) throw Error(noSource(id));
}

std::vector<PaidExtra> Income::postWithPay(const Date& date, PeriodId period) {
    std::vector<IncomeSource> rows;
    {
        auto st = db_.prepare(std::string(kSelectSource) + "WHERE s.is_deleted = 0 AND s.frequency = 1 ORDER BY s.id");
        while (st.step()) rows.push_back(readSource(st));
    }
    std::vector<PaidExtra> out;
    for (const IncomeSource& source : rows) {
        if (source.potId) {
            detail::insertLedger(db_, date, *source.potId, source.usualAmount, LedgerKind::AddedToSavings,
                                 source.name, period);
        } else {
            detail::insertIncome(db_, period, date, source.name, source.usualAmount);
        }
        detail::writeLog(db_, LogKind::Pay, "With pay: " + source.name + ", KES " + formatMoney(source.usualAmount) +
                                                (source.potId ? " into " + source.goesTo : ""));
        out.push_back(PaidExtra{source.name, source.usualAmount, source.goesTo});
    }
    return out;
}

} // namespace pesa