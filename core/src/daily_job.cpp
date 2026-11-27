// FTracker/core/src/daily_job.cpp
#include <pesa/daily_job.h>
#include "corrections_sql.h"
#include <pesa/rules.h>

#include "db.h"
#include "entry_sql.h"
#include "log_sql.h"

#include <vector>

namespace pesa {
    namespace {
        void saveLastPosted(detail::Database &db, const Date &date) {
            db.prepare("INSERT INTO meta(key, value) VALUES ('last_posted', ?) "
                        "ON CONFLICT(key) DO UPDATE SET value = excluded.value")
                    .bind(1, date.toString())
                    .run();
        }

        // A day the job already finished: flagged AND with a saved total. (Flag set but no total
        // means something went wrong, so that day is simply done again.)
        bool alreadyCalculated(detail::Database &db, const Date &date) {
            auto st = db.prepare("SELECT 1 FROM days WHERE date = ? AND calculated = 1 AND result IS NOT NULL");
            st.bind(1, date.toString());
            return st.step();
        }

        // Copies one catalog item into the day as an auto entry. Returns true if a row was added.
        // The WHERE clause is the whole policy:
        //   - the item and its category must be usable (not paused, not deleted)
        //   - nothing from that item is on that day yet (posted earlier, or added by you by hand)
        struct PostedItem {
            enum class Outcome { Nothing, Auto, Skipped } outcome = Outcome::Nothing;

            std::string skipName;
        };

        // Decides what happens for one item on one day, and does it:
        //   nothing - the item or its category is paused or deleted, or the day already has that item
        //   auto    - a normal entry, at the item's default plus any adjust rules
        //   skipped - a skip covers it: a grey row is written, at the amount it WOULD have been
        PostedItem postItem(detail::Database &db, const Date &date, RuleId rule, const RuleItem &item) {
            CategoryId category = 0;
            {
                auto st = db.prepare(
                    "SELECT i.category_id FROM items i JOIN categories c ON c.id = i.category_id "
                    "WHERE i.id = ?1 AND i.is_deleted = 0 AND i.is_active = 1 "
                    "AND c.is_deleted = 0 AND c.is_active = 1 "
                    "AND (i.expires_on IS NULL OR i.expires_on >= ?2) AND (c.expires_on IS NULL OR c.expires_on >= ?2) "
                    "AND NOT EXISTS (SELECT 1 FROM entries e WHERE e.date = ?2 AND e.item_id = i.id)");
                st.bind(1, item.id).bind(2, date.toString());
                if (!st.step()) return {};
                category = st.integer(0);
            } // this query is finished before we write

            const Money amount = detail::postAmount(db, date, item.id);
            const auto skip = detail::activeSkip(db, date, item.id);

            auto insert = db.prepare(
                "INSERT INTO entries(date, item_id, category_id, name, amount, kind, funded_from, rule_id, skip_id) "
                "VALUES (?, ?, ?, ?, ?, ?, 0, ?, ?)");
            insert.bind(1, date.toString())
                    .bind(2, item.id)
                    .bind(3, category)
                    .bind(4, item.name)
                    .bind(5, amount)
                    .bind(6, static_cast<std::int64_t>(skip ? EntryKind::Skipped : EntryKind::AutoPosted))
                    .bind(7, rule);
            if (skip) insert.bind(8, skip->id);
            else insert.bindNull(8);
            insert.run();

            if (skip) return PostedItem{PostedItem::Outcome::Skipped, skip->name};
            return PostedItem{PostedItem::Outcome::Auto, {}};
        }

        Money postedTotal(detail::Database &db, const Date &date) {
            auto st = db.prepare("SELECT COALESCE(SUM(amount), 0) FROM entries "
                "WHERE date = ? AND rule_id IS NOT NULL AND kind = 0");
            st.bind(1, date.toString());
            st.step();
            return st.integer(0);
        }

        std::string joinNames(const std::vector<std::string> &names) {
            std::string out;
            for (const std::string &name: names) out += (out.empty() ? "" : ", ") + name;
            return out;
        }

        // For lines written OUTSIDE a day's transaction: a failed log line must never stop the job.
        void tryLog(detail::Database &db, LogKind kind, std::string_view message, bool once = false) {
            try {
                if (once) detail::writeLogOnce(db, kind, message);
                else detail::writeLog(db, kind, message);
            } catch (...) {
            }
        }
    } // namespace

    std::optional<Date> DailyJob::lastPosted() const {
        auto st = db_.prepare("SELECT value FROM meta WHERE key = 'last_posted'");
        if (!st.step()) return std::nullopt;
        return Date::parse(st.text(0));
    }

    void DailyJob::postDay(const Date &date, DailyJobResult &result) {
        // One transaction per day: a crash leaves the day fully posted or not at all, and
        // "last posted" and the log lines move in the same step, so they can never disagree.
        detail::Transaction tx(db_);
        std::vector<std::string> names; // posted normally
        std::vector<std::string> skipped; // "Tea (Public holiday)"
        Money amount = 0;

        if (!alreadyCalculated(db_, date)) {
            detail::ensureDay(db_, date);
            const Rules rules(db_);
            for (const Rule &rule: rules.list(Visibility::ActiveOnly)) {
                if (rule.kind != RuleKind::Charge || !rule.appliesOn(date)) continue;
                // adjust rules only change amounts
                for (const RuleItem &item: rule.items) {
                    const PostedItem posted = postItem(db_, date, rule.id, item);
                    if (posted.outcome == PostedItem::Outcome::Auto) names.push_back(item.name);
                    else if (posted.outcome == PostedItem::Outcome::Skipped)
                        skipped.push_back(
                            item.name + " (" + posted.skipName + ")");
                }
            }
            db_.prepare("UPDATE days SET calculated = 1 WHERE date = ?").bind(1, date.toString()).run();
            detail::recalculateDay(db_, date);
            amount = postedTotal(db_, date);
            if (!names.empty()) {
                detail::writeLog(db_, LogKind::Rule, "Auto-posted " + joinNames(names) + ": KES " +
                                                     formatMoney(amount) + " for " + date.toString());
            }
            if (!skipped.empty()) {
                detail::writeLog(db_, LogKind::Skip, "Skipped " + joinNames(skipped) + " on " + date.toString());
            }
        }
        saveLastPosted(db_, date);
        tx.commit();

        result.entries += static_cast<int>(names.size());
        result.skipped += static_cast<int>(skipped.size());
        result.total += amount;
    }

    DailyJobResult DailyJob::run(const Date &today, TimeOfDay now) {
        DailyJobResult result;
        if (!settings_.autoPostEnabled()) return result;

        // Clock guard: a computer date earlier than the last posted day would put entries on
        // the wrong days, so we stop and say so (once, not on every launch).
        const auto last = lastPosted();
        if (last && today < *last) {
            result.clockBehind = true;
            tryLog(db_, LogKind::Issue,
                   "Posting paused: this computer's date (" + today.toString() +
                   ") is earlier than the last day Pesa posted (" + last->toString() + ")",
                   true);
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
            } catch (const Error &e) {
                // The day's transaction was rolled back and "last posted" did not move,
                // so the next launch tries this same day again.
                result.problem = e.what();
                tryLog(db_, LogKind::Issue, "Could not post " + day.toString() + ": " + e.what(), true);
                break;
            }
            ++result.days;
            if (!result.from) result.from = day;
            result.to = day;
        }

        if (result.problem.empty() && result.days > 1) {
            tryLog(db_, LogKind::Rule, "Catch-up: posted " + std::to_string(result.days) + " days (" +
                                       result.from->toString() + " to " + result.to->toString() + "), " +
                                       std::to_string(result.entries) + " entries, KES " + formatMoney(result.total));
        }
        return result;
    }
} // namespace pesa
