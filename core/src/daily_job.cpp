//
// Created by lee on 11/24/26.
//
// FTracker/core/src/daily_job.cpp
#include <pesa/daily_job.h>

#include <pesa/rules.h>

#include "db.h"
#include "entry_sql.h"

namespace pesa {

namespace {

void saveLastPosted(detail::Database& db, const Date& date) {
    db.prepare("INSERT INTO meta(key, value) VALUES ('last_posted', ?) "
               "ON CONFLICT(key) DO UPDATE SET value = excluded.value")
        .bind(1, date.toString())
        .run();
}

// A day the job already finished: flagged AND with a saved total. (Flag set but no total
// means something went wrong, so that day is simply done again.)
bool alreadyCalculated(detail::Database& db, const Date& date) {
    auto st = db.prepare("SELECT 1 FROM days WHERE date = ? AND calculated = 1 AND result IS NOT NULL");
    st.bind(1, date.toString());
    return st.step();
}

// Copies one catalog item into the day as an auto entry. Returns true if a row was added.
// The WHERE clause is the whole policy:
//   - the item and its category must be usable (not paused, not deleted)
//   - nothing from that item is on that day yet (posted earlier, or added by you by hand)
bool postItem(detail::Database& db, const Date& date, RuleId rule, ItemId item) {
    db.prepare(
        "INSERT INTO entries(date, item_id, category_id, name, amount, kind, funded_from, rule_id) "
        "SELECT ?1, i.id, i.category_id, i.name, i.default_amount, 0, 0, ?3 "
        "FROM items i JOIN categories c ON c.id = i.category_id "
        "WHERE i.id = ?2 AND i.is_deleted = 0 AND i.is_active = 1 "
        "AND c.is_deleted = 0 AND c.is_active = 1 "
        "AND NOT EXISTS (SELECT 1 FROM entries e WHERE e.date = ?1 AND e.item_id = i.id)")
        .bind(1, date.toString())
        .bind(2, item)
        .bind(3, rule)
        .run();
    return db.changes() > 0;
}

Money postedTotal(detail::Database& db, const Date& date) {
    auto st = db.prepare("SELECT COALESCE(SUM(amount), 0) FROM entries "
                         "WHERE date = ? AND rule_id IS NOT NULL AND kind = 0");
    st.bind(1, date.toString());
    st.step();
    return st.integer(0);
}

// Best effort: a problem while writing the problem must not hide the original one.
void recordIssue(detail::Database& db, const Date& date, std::string_view message) {
    try {
        db.prepare("INSERT INTO issues(day, message) VALUES (?, ?)")
            .bind(1, date.toString()).bind(2, message).run();
    } catch (...) {}
}

} // namespace

std::optional<Date> DailyJob::lastPosted() const {
    auto st = db_.prepare("SELECT value FROM meta WHERE key = 'last_posted'");
    if (!st.step()) return std::nullopt;
    return Date::parse(st.text(0));
}

void DailyJob::postDay(const Date& date, DailyJobResult& result) {
    // One transaction per day: a crash leaves the day fully posted or not at all, and
    // "last posted" moves in the same step, so the two can never disagree.
    detail::Transaction tx(db_);
    int posted = 0;
    Money amount = 0;

    if (!alreadyCalculated(db_, date)) {
        detail::ensureDay(db_, date);
        const Rules rules(db_);
        for (const Rule& rule : rules.list(Visibility::ActiveOnly)) {
            if (!rule.appliesOn(date)) continue;
            for (const RuleItem& item : rule.items) {
                if (postItem(db_, date, rule.id, item.id)) ++posted;
            }
        }
        db_.prepare("UPDATE days SET calculated = 1 WHERE date = ?").bind(1, date.toString()).run();
        detail::recalculateDay(db_, date);
        amount = postedTotal(db_, date);
    }
    saveLastPosted(db_, date);
    tx.commit();

    result.entries += posted;
    result.total += amount;
}

DailyJobResult DailyJob::run(const Date& today, TimeOfDay now) {
    DailyJobResult result;
    if (!settings_.autoPostEnabled()) return result;

    // Clock guard: a computer date earlier than the last posted day would put entries on
    // the wrong days, so we stop and say so.
    const auto last = lastPosted();
    if (last && today < *last) {
        result.clockBehind = true;
        return result;
    }
    result.ran = true;

    // Today only counts once "post after" has passed. Earlier days have already happened.
    const Date through = (now >= settings_.postAfter()) ? today : today.previousDay();
    // A first run starts at the newest postable day: it does not rewrite history.
    const Date first = last ? last->nextDay() : through;

    for (Date day = first; day <= through; day = day.nextDay()) {
        try {
            postDay(day, result);
        } catch (const Error& e) {
            // The day's transaction was rolled back and "last posted" did not move,
            // so the next launch tries this same day again.
            result.problem = e.what();
            recordIssue(db_, day, e.what());
            break;
        }
        ++result.days;
        if (!result.from) result.from = day;
        result.to = day;
    }
    return result;
}

} // namespace pesa