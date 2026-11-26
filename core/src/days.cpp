// FTracker/core/src/days.cpp
#include <pesa/days.h>
#include "corrections_sql.h"
#include "log_sql.h"
#include "db.h"
#include "entry_sql.h"

namespace pesa {
    DayView Days::view(const Date &date) const {
        DayView view;
        view.date = date;

        {
            auto st = db_.prepare(std::string(detail::kSelectEntry) +
                                  "WHERE e.date = ? ORDER BY (e.category_id IS NULL), c.is_system, c.sort_order, c.id, e.id");
            st.bind(1, date.toString());
            while (st.step()) view.entries.push_back(detail::readEntry(st));
        }

        for (const Entry &e: view.entries) {
            if (e.kind == EntryKind::Skipped) continue; // shown in the list, never counted
            (e.fund == FundedFrom::Earned ? view.earnedTotal : view.savingsTotal) += e.amount;
        }

        auto st = db_.prepare("SELECT calculated FROM days WHERE date = ?");
        st.bind(1, date.toString());
        view.calculated = st.step() && st.integer(0) != 0;
        return view;
    }

    void Days::recalculate(const Date &date) {
        detail::Transaction tx(db_);
        detail::recalculateDay(db_, date);
        tx.commit();
    }

    std::optional<Money> Days::savedResult(const Date &date) const {
        auto st = db_.prepare("SELECT result FROM days WHERE date = ?");
        st.bind(1, date.toString());
        if (!st.step() || st.isNull(0)) return std::nullopt;
        return st.integer(0);
    }

    CorrectionResult Days::wipe(const Date &from, const Date &to, const Date &today, Mode mode) {
        if (to < from) throw Error("The last day is before the first day.");
        if (to > today) {
            throw Error("Wipe all is for days that already passed. The last day can be " + today.toString() +
                        " at the latest.");
        }
        detail::requireOwnTransactionForPreview(db_, mode);

        detail::Transaction tx(db_); // Preview: never committed, so everything below is rolled back
        const auto whereKind = [](int kind) { return "date BETWEEN ?1 AND ?2 AND kind = " + std::to_string(kind); };
        const detail::Binder bind = [&](detail::Statement &st) {
            st.bind(1, from.toString()).bind(2, to.toString());
        };

        CorrectionResult result;
        const auto doomed = detail::match(db_, whereKind(0), bind); // untouched auto entries
        auto &c = result.changes;
        c.entries = doomed.entries;
        c.days = doomed.days;
        c.before = doomed.total;
        c.after = 0;
        c.sample = doomed.sample;
        c.keptEdited = detail::countMatching(db_, whereKind(1), bind);
        c.keptAdded = detail::countMatching(db_, whereKind(2), bind);

        if (doomed.entries > 0) {
            result.restorePoint = detail::saveRestorePoint(
                db_, "Before wiping " + from.toString() + " to " + to.toString(), from, to);
            // An entry paid from savings is refunded: its ledger line goes with it.
            db_.prepare("DELETE FROM ledger WHERE entry_id IN (SELECT id FROM entries WHERE " + whereKind(0) + ")")
                    .bind(1, from.toString()).bind(2, to.toString()).run();
            db_.prepare("DELETE FROM entries WHERE " + whereKind(0))
                    .bind(1, from.toString()).bind(2, to.toString()).run();
            detail::recalculateRange(db_, from, to);
            detail::writeLog(db_, LogKind::Skip,
                             "Wiped " + std::to_string(c.entries) + " rule-posted entries (KES " +
                             formatMoney(c.before) + ") from " + from.toString() + " to " + to.toString());
        }

        if (mode == Mode::Apply) {
            tx.commit();
        } else {
            result.restorePoint.reset();
        }
        return result;
    }
} // namespace pesa
