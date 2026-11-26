//
// Created by lee on 11/26/26.
//
// FTracker/core/src/restore_points.cpp
#include <pesa/restore_points.h>

#include "corrections_sql.h"
#include "db.h"
#include "log_sql.h"
#include "money_sql.h"

namespace pesa {
    std::vector<RestorePoint> RestorePoints::list() const {
        auto st = db_.prepare(
            "SELECT p.id, p.label, p.created_at, p.from_date, p.to_date, "
            "(SELECT COUNT(*) FROM rp_entries r WHERE r.restore_point_id = p.id) "
            "FROM restore_points p ORDER BY p.id DESC");
        std::vector<RestorePoint> out;
        while (st.step()) {
            const std::string stamp = st.text(2); // "2026-11-24 18:40:12"
            out.push_back(RestorePoint{
                .id = st.integer(0),
                .label = st.text(1),
                .date = Date::parse(stamp.substr(0, 10)),
                .time = stamp.substr(11, 5),
                .from = Date::parse(st.text(3)),
                .to = Date::parse(st.text(4)),
                .entries = static_cast<int>(st.integer(5)),
            });
        }
        return out;
    }

    RestoreResult RestorePoints::restore(RestorePointId id) {
        std::optional<RestorePoint> point;
        for (RestorePoint &p: list()) {
            if (p.id == id) point = std::move(p);
        }
        if (!point) throw Error("No restore point with id " + std::to_string(id) + ".");

        detail::Transaction tx(db_); // all of it, or none of it
        RestoreResult result;
        result.label = point->label;
        // Going back is itself undoable. Pruning waits until the end, so the point we are using survives.
        result.undoPoint = detail::saveRestorePoint(db_, "Before going back to: " + point->label,
                                                    point->from, point->to, false);

        // 1. Rules and skips. Anything newer than the snapshot is switched off first (never deleted:
        //    entries may point at it), then the saved state is put back.
        db_.prepare("UPDATE rules SET is_deleted = 1 WHERE id NOT IN "
                    "(SELECT rule_id FROM rp_rules WHERE restore_point_id = ?1)")
                .bind(1, id).run();
        db_.prepare("UPDATE rules SET "
                    "is_active = (SELECT s.is_active FROM rp_rules s WHERE s.restore_point_id = ?1 AND s.rule_id = rules.id), "
                    "is_deleted = (SELECT s.is_deleted FROM rp_rules s WHERE s.restore_point_id = ?1 AND s.rule_id = rules.id), "
                    "effective_to = (SELECT s.effective_to FROM rp_rules s WHERE s.restore_point_id = ?1 AND s.rule_id = rules.id) "
                    "WHERE id IN (SELECT rule_id FROM rp_rules WHERE restore_point_id = ?1)")
                .bind(1, id).run();
        db_.prepare("UPDATE skips SET is_deleted = 1 WHERE id NOT IN "
                    "(SELECT skip_id FROM rp_skips WHERE restore_point_id = ?1)")
                .bind(1, id).run();
        db_.prepare("UPDATE skips SET "
                    "is_deleted = (SELECT s.is_deleted FROM rp_skips s WHERE s.restore_point_id = ?1 AND s.skip_id = skips.id) "
                    "WHERE id IN (SELECT skip_id FROM rp_skips WHERE restore_point_id = ?1)")
                .bind(1, id).run();

        // 2. The entries of those days: out with the current ones, in with the saved ones.
        const detail::Binder range = [&](detail::Statement &st) {
            st.bind(1, point->from.toString()).bind(2, point->to.toString());
        };
        {
            auto st = db_.prepare("DELETE FROM ledger WHERE entry_id IN "
                "(SELECT id FROM entries WHERE date BETWEEN ?1 AND ?2)");
            range(st);
            st.run();
        }
        {
            auto st = db_.prepare("DELETE FROM entries WHERE date BETWEEN ?1 AND ?2");
            range(st);
            st.run();
        }
        db_.prepare("INSERT INTO entries(id, date, item_id, category_id, name, amount, original_amount, kind, "
                    "funded_from, rule_id, skip_id) "
                    "SELECT id, date, item_id, category_id, name, amount, original_amount, kind, "
                    "funded_from, rule_id, skip_id FROM rp_entries WHERE restore_point_id = ?")
                .bind(1, id).run();
        result.entries = db_.changes();

        // 3. The ledger follows the entries: whatever was paid from savings is charged again.
        for (const EntryId entry: detail::idsMatching(db_, "date BETWEEN ?1 AND ?2 AND funded_from = 1 AND kind != 3",
                                                      range)) {
            detail::syncExpenseCover(db_, entry);
        }
        detail::recalculateRange(db_, point->from, point->to);

        // 4. A skip that is gone now may still have grey rows on days outside the range: bring them back.
        std::vector<SkipId> stale;
        {
            auto st = db_.prepare("SELECT DISTINCT skip_id FROM entries WHERE kind = 3 AND skip_id IN "
                "(SELECT id FROM skips WHERE is_deleted = 1)");
            while (st.step()) stale.push_back(st.integer(0));
        }
        for (const SkipId skip: stale) detail::unskipMarkers(db_, skip);

        detail::pruneRestorePoints(db_);
        detail::writeLog(db_, LogKind::Edit, "Went back to restore point: " + point->label + " (" +
                                             std::to_string(result.entries) + " entries)");
        tx.commit();
        return result;
    }
} // namespace pesa
