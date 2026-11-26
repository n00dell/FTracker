//
// Created by lee on 11/26/26.
//
// FTracker/core/src/skips.cpp
#include <pesa/skips.h>

#include "corrections_sql.h"
#include "db.h"
#include "log_sql.h"
#include "money_sql.h"
#include "util.h"

#include <algorithm>

namespace pesa {
    namespace {
        constexpr const char *kSelectSkip = "SELECT id, name, from_date, to_date, is_deleted FROM skips ";

        Skip readSkip(const detail::Statement &st) {
            Skip s;
            s.id = st.integer(0);
            s.name = st.text(1);
            s.from = Date::parse(st.text(2));
            s.to = Date::parse(st.text(3));
            s.isDeleted = st.integer(4) != 0;
            return s;
        }

        std::vector<SkipTarget> loadTargets(detail::Database &db, SkipId id) {
            auto st = db.prepare(
                "SELECT t.category_id, t.item_id, COALESCE(c.name, i.name) FROM skip_targets t "
                "LEFT JOIN categories c ON c.id = t.category_id LEFT JOIN items i ON i.id = t.item_id "
                "WHERE t.skip_id = ? ORDER BY t.rowid");
            st.bind(1, id);
            std::vector<SkipTarget> out;
            while (st.step()) {
                SkipTarget t;
                if (!st.isNull(0)) t.categoryId = st.integer(0);
                if (!st.isNull(1)) t.itemId = st.integer(1);
                t.name = st.text(2);
                out.push_back(std::move(t));
            }
            return out;
        }
    } // namespace

    std::optional<Skip> Skips::find(SkipId id) const {
        std::optional<Skip> skip;
        {
            auto st = db_.prepare(std::string(kSelectSkip) + "WHERE id = ?");
            st.bind(1, id);
            if (st.step()) skip = readSkip(st);
        }
        if (skip) skip->targets = loadTargets(db_, id);
        return skip;
    }

    std::vector<Skip> Skips::list(bool includeRemoved) const {
        std::vector<Skip> out;
        {
            auto st = db_.prepare(std::string(kSelectSkip) + (includeRemoved ? "" : "WHERE is_deleted = 0 ") +
                                  "ORDER BY from_date, id");
            while (st.step()) out.push_back(readSkip(st));
        }
        for (Skip &skip: out) skip.targets = loadTargets(db_, skip.id);
        return out;
    }

    CorrectionResult Skips::add(const NewSkip &skip, const Date &today, Mode mode) {
        const std::string name = detail::cleanName(skip.name, "A skip needs a name.");
        if (skip.to < skip.from) throw Error("A skip cannot end before it starts.");
        if (skip.categories.empty() && skip.items.empty()) {
            throw Error("Say what to skip: at least one category or item.");
        }
        detail::requireOwnTransactionForPreview(db_, mode);

        detail::Transaction tx(db_); // Preview: never committed, so everything below is rolled back
        for (const CategoryId category: skip.categories) {
            auto st = db_.prepare("SELECT 1 FROM categories WHERE id = ? AND is_deleted = 0");
            st.bind(1, category);
            if (!st.step()) throw Error("No category with id " + std::to_string(category) + ".");
        }
        for (const ItemId item: skip.items) {
            auto st = db_.prepare("SELECT 1 FROM items WHERE id = ? AND is_deleted = 0");
            st.bind(1, item);
            if (!st.step()) throw Error("No item with id " + std::to_string(item) + ".");
        }

        CorrectionResult result;
        const Date upTo = std::min(skip.to, today); // only days that have posted can be changed
        const bool touchesPast = skip.from <= today;
        if (touchesPast) {
            result.restorePoint = detail::saveRestorePoint(db_, "Before skip '" + name + "'", skip.from, upTo);
        }

        db_.prepare("INSERT INTO skips(name, from_date, to_date) VALUES (?, ?, ?)")
                .bind(1, name).bind(2, skip.from.toString()).bind(3, skip.to.toString()).run();
        const SkipId id = db_.lastInsertId();
        for (const CategoryId category: skip.categories) {
            db_.prepare("INSERT INTO skip_targets(skip_id, category_id) VALUES (?, ?)").bind(1, id).bind(2, category).
                    run();
        }
        for (const ItemId item: skip.items) {
            db_.prepare("INSERT INTO skip_targets(skip_id, item_id) VALUES (?, ?)").bind(1, id).bind(2, item).run();
        }
        result.created = id;

        if (touchesPast) {
            // Entries of the covered items or categories, inside the dates, of one kind.
            const auto whereKind = [](int kind) {
                return "kind = " + std::to_string(kind) + " AND date BETWEEN ?1 AND ?2 AND "
                       "(item_id IN (SELECT item_id FROM skip_targets WHERE skip_id = ?3 AND item_id IS NOT NULL) OR "
                       "category_id IN (SELECT category_id FROM skip_targets WHERE skip_id = ?3 AND category_id IS NOT NULL))";
            };
            const detail::Binder bind = [&](detail::Statement &st) {
                st.bind(1, skip.from.toString()).bind(2, upTo.toString()).bind(3, id);
            };
            const std::string where = whereKind(0); // untouched auto entries

            const auto hit = detail::match(db_, where, bind);
            auto &c = result.changes;
            c.entries = hit.entries;
            c.days = hit.days;
            c.before = hit.total;
            c.after = 0; // grey rows are not counted
            c.keptEdited = detail::countMatching(db_, whereKind(1), bind);
            c.keptAdded = detail::countMatching(db_, whereKind(2), bind);
            c.sample = hit.sample; // their `after` is already 0

            const auto paidFromSavings = detail::idsMatching(db_, where + " AND funded_from = 1", bind);
            {
                auto st = db_.prepare("UPDATE entries SET kind = 3, skip_id = ?4 WHERE " + where);
                bind(st);
                st.bind(4, id);
                st.run();
            }
            for (const EntryId entry: paidFromSavings) detail::syncExpenseCover(db_, entry); // refunds the pot
            detail::recalculateRange(db_, skip.from, upTo);

            if (c.entries > 0) {
                detail::writeLog(db_, LogKind::Skip,
                                 "Skip '" + name + "' (" + skip.from.toString() + " to " + skip.to.toString() +
                                 ") marked " + std::to_string(c.entries) + " posted entries as skipped (KES " +
                                 formatMoney(c.before) + " no longer counted)");
            }
        }
        if (!touchesPast || result.changes.entries == 0) {
            detail::writeLog(db_, LogKind::Skip, "Added skip '" + name + "' (" + skip.from.toString() + " to " +
                                                 skip.to.toString() + ")");
        }

        if (mode == Mode::Apply) {
            tx.commit();
        } else {
            result.restorePoint.reset(); // it was rolled back with everything else
            result.created = 0;
        }
        return result;
    }

    CorrectionResult Skips::remove(SkipId id, Mode mode) {
        detail::requireOwnTransactionForPreview(db_, mode);
        detail::Transaction tx(db_);

        std::string name;
        {
            auto st = db_.prepare("SELECT name FROM skips WHERE id = ? AND is_deleted = 0");
            st.bind(1, id);
            if (!st.step()) throw Error("No skip with id " + std::to_string(id) + ".");
            name = st.text(0);
        }

        CorrectionResult result;
        {
            // Which days hold grey rows of this skip?
            auto st = db_.prepare("SELECT COUNT(*), MIN(date), MAX(date) FROM entries WHERE skip_id = ? AND kind = 3");
            st.bind(1, id);
            st.step();
            if (st.integer(0) > 0) {
                result.restorePoint = detail::saveRestorePoint(db_, "Before removing skip '" + name + "'",
                                                               Date::parse(st.text(1)), Date::parse(st.text(2)));
            }
        }
        db_.prepare("UPDATE skips SET is_deleted = 1 WHERE id = ?").bind(1, id).run();
        const auto back = detail::unskipMarkers(db_, id); // after the skip is marked deleted

        result.changes.entries = back.entries;
        result.changes.days = back.days;
        result.changes.before = 0;
        result.changes.after = back.total;
        detail::writeLog(db_, LogKind::Skip, "Removed skip '" + name + "'" +
                                             (back.entries > 0
                                                  ? ": " + std::to_string(back.entries) +
                                                    " entries (KES " + formatMoney(back.total) +
                                                    ") count again"
                                                  : ""));

        if (mode == Mode::Apply) {
            tx.commit();
        } else {
            result.restorePoint.reset();
        }
        return result;
    }
} // namespace pesa
