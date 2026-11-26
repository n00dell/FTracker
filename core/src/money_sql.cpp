//
// Created by lee on 11/25/26.
//
// FTracker/core/src/money_sql.cpp
#include "money_sql.h"

#include <pesa/common.h>

namespace pesa::detail {
    PotId mainPotId(Database &db) {
        auto st = db.prepare("SELECT id FROM pots WHERE is_main = 1");
        if (!st.step()) throw Error("The built-in Main savings pot is missing.");
        return st.integer(0);
    }

    std::string requirePot(Database &db, PotId id) {
        auto st = db.prepare("SELECT name FROM pots WHERE id = ?");
        st.bind(1, id);
        if (!st.step()) throw Error("No pot with id " + std::to_string(id) + ".");
        return st.text(0);
    }

    void insertLedger(Database &db, const Date &date, PotId pot, Money amount, LedgerKind kind,
                      std::string_view note, std::optional<PeriodId> period) {
        auto st = db.prepare("INSERT INTO ledger(date, pot_id, amount, kind, note, period_id) "
            "VALUES (?, ?, ?, ?, ?, ?)");
        st.bind(1, date.toString())
                .bind(2, pot)
                .bind(3, amount)
                .bind(4, static_cast<std::int64_t>(kind))
                .bind(5, note);
        if (period) st.bind(6, *period);
        else st.bindNull(6);
        st.run();
    }

    void insertIncome(Database &db, PeriodId period, const Date &date, std::string_view label, Money amount) {
        db.prepare("INSERT INTO income(period_id, date, label, amount) VALUES (?, ?, ?, ?)")
                .bind(1, period).bind(2, date.toString()).bind(3, label).bind(4, amount).run();
    }

    void dropExpenseCover(Database &db, EntryId entry) {
        db.prepare("DELETE FROM ledger WHERE entry_id = ?").bind(1, entry).run();
    }

    void syncExpenseCover(Database &db, EntryId entry) {
        dropExpenseCover(db, entry);
        // INSERT ... SELECT: the line is built from the entry itself, so it can never disagree
        // with it. Nothing is inserted unless the entry is paid from savings and not skipped.
        db.prepare("INSERT INTO ledger(date, pot_id, amount, kind, note, entry_id) "
                   "SELECT e.date, (SELECT id FROM pots WHERE is_main = 1), -e.amount, " +
                   std::to_string(static_cast<int>(LedgerKind::ExpenseCover)) + ", e.name, e.id "
                   "FROM entries e WHERE e.id = ?1 AND e.funded_from = 1 AND e.kind != 3")
                .bind(1, entry)
                .run();
    }
} // namespace pesa::detail