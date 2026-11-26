// FTracker/core/src/corrections_sql.cpp
#include "corrections_sql.h"

#include "money_sql.h"

#include <algorithm>

namespace pesa::detail {
    void requireOwnTransactionForPreview(Database &db, Mode mode) {
        if (mode == Mode::Preview && db.inTransaction()) {
            throw Error("A preview cannot run inside another change.");
        }
    }

    Matched match(Database &db, const std::string &where, const Binder &bind) {
        Matched m;
        {
            auto st = db.prepare("SELECT COUNT(*), COUNT(DISTINCT date), COALESCE(SUM(amount), 0) "
                                 "FROM entries WHERE " + where);
            bind(st);
            st.step();
            m.entries = static_cast<int>(st.integer(0));
            m.days = static_cast<int>(st.integer(1));
            m.total = st.integer(2);
        }
        {
            auto st = db.prepare("SELECT date, name, amount FROM entries WHERE " + where +
                                 " ORDER BY date, id LIMIT 5");
            bind(st);
            while (st.step()) {
                m.sample.push_back(ChangedEntry{Date::parse(st.text(0)), st.text(1), st.integer(2), 0});
            }
        }
        return m;
    }

    int countMatching(Database &db, const std::string &where, const Binder &bind) {
        auto st = db.prepare("SELECT COUNT(*) FROM entries WHERE " + where);
        bind(st);
        st.step();
        return static_cast<int>(st.integer(0));
    }

    std::vector<EntryId> idsMatching(Database &db, const std::string &where, const Binder &bind) {
        auto st = db.prepare("SELECT id FROM entries WHERE " + where + " ORDER BY id");
        bind(st);
        std::vector<EntryId> ids;
        while (st.step()) ids.push_back(st.integer(0));
        return ids;
    }

    void recalculateRange(Database &db, const Date &from, const Date &to) {
        db.prepare("UPDATE days SET result = "
                    "(SELECT COALESCE(SUM(amount), 0) FROM entries WHERE entries.date = days.date AND kind != 3) "
                    "WHERE date BETWEEN ?1 AND ?2")
                .bind(1, from.toString())
                .bind(2, to.toString())
                .run();
    }

    // ---------------- restore points ----------------

    void pruneRestorePoints(Database &db) {
        // ON DELETE CASCADE removes the snapshot rows together with their restore point.
        db.prepare("DELETE FROM restore_points WHERE id NOT IN "
                    "(SELECT id FROM restore_points ORDER BY id DESC LIMIT 5)")
                .run();
    }

    RestorePointId saveRestorePoint(Database &db, std::string_view label, const Date &from, const Date &to,
                                    bool prune) {
        db.prepare("INSERT INTO restore_points(label, from_date, to_date) VALUES (?, ?, ?)")
                .bind(1, label).bind(2, from.toString()).bind(3, to.toString()).run();
        const RestorePointId id = db.lastInsertId();

        db.prepare("INSERT INTO rp_entries(restore_point_id, id, date, item_id, category_id, name, amount, "
                    "original_amount, kind, funded_from, rule_id, skip_id) "
                    "SELECT ?1, id, date, item_id, category_id, name, amount, original_amount, kind, "
                    "funded_from, rule_id, skip_id FROM entries WHERE date BETWEEN ?2 AND ?3")
                .bind(1, id).bind(2, from.toString()).bind(3, to.toString()).run();
        db.prepare("INSERT INTO rp_rules(restore_point_id, rule_id, is_active, is_deleted, effective_to) "
                    "SELECT ?1, id, is_active, is_deleted, effective_to FROM rules")
                .bind(1, id).run();
        db.prepare("INSERT INTO rp_skips(restore_point_id, skip_id, is_deleted) "
                    "SELECT ?1, id, is_deleted FROM skips")
                .bind(1, id).run();

        if (prune) pruneRestorePoints(db);
        return id;
    }

    // ---------------- what the daily job posts ----------------

    Money postAmount(Database &db, const Date &date, ItemId item) {
        auto st = db.prepare(
            "SELECT i.default_amount + COALESCE((SELECT SUM(r.extra) FROM rules r "
            "JOIN rule_items ri ON ri.rule_id = r.id "
            "WHERE r.kind = 1 AND r.is_active = 1 AND r.is_deleted = 0 AND ri.item_id = i.id "
            "AND r.effective_from <= ?1 AND (r.effective_to IS NULL OR r.effective_to >= ?1)), 0) "
            "FROM items i WHERE i.id = ?2");
        st.bind(1, date.toString()).bind(2, item);
        if (!st.step()) throw Error("No item with id " + std::to_string(item) + ".");
        return std::max<Money>(st.integer(0), 1); // an entry can never be 0 or below
    }

    std::optional<ActiveSkip> activeSkip(Database &db, const Date &date, ItemId item) {
        auto st = db.prepare(
            "SELECT s.id, s.name FROM skips s "
            "JOIN skip_targets t ON t.skip_id = s.id "
            "JOIN items i ON i.id = ?2 "
            "WHERE s.is_deleted = 0 AND s.from_date <= ?1 AND s.to_date >= ?1 "
            "AND (t.item_id = i.id OR t.category_id = i.category_id) "
            "ORDER BY s.id LIMIT 1");
        st.bind(1, date.toString()).bind(2, item);
        if (!st.step()) return std::nullopt;
        return ActiveSkip{st.integer(0), st.text(1)};
    }

    Unskipped unskipMarkers(Database &db, SkipId skip) {
        struct Row {
            EntryId id;
            Date date;
            ItemId item;
        };
        std::vector<Row> rows;
        {
            auto st = db.prepare("SELECT id, date, item_id FROM entries WHERE skip_id = ? AND kind = 3 "
                "ORDER BY date, id");
            st.bind(1, skip);
            while (st.step()) rows.push_back(Row{st.integer(0), Date::parse(st.text(1)), st.integer(2)});
        }

        Unskipped out;
        std::optional<Date> lastDay;
        for (const Row &row: rows) {
            const Money amount = postAmount(db, row.date, row.item); // what posting would do now
            if (const auto other = activeSkip(db, row.date, row.item)) {
                // Another skip still covers it: stay grey, now under that skip.
                db.prepare("UPDATE entries SET skip_id = ?, amount = ? WHERE id = ?")
                        .bind(1, other->id).bind(2, amount).bind(3, row.id).run();
                continue;
            }
            db.prepare("UPDATE entries SET kind = 0, skip_id = NULL, amount = ? WHERE id = ?")
                    .bind(1, amount).bind(2, row.id).run();
            syncExpenseCover(db, row.id); // an entry paid from savings counts against the pot again
            ++out.entries;
            out.total += amount;
            if (!lastDay || *lastDay != row.date) {
                ++out.days;
                lastDay = row.date;
            }
        }
        if (!rows.empty()) recalculateRange(db, rows.front().date, rows.back().date);
        return out;
    }
} // namespace pesa::detail
