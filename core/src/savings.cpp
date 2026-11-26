//
// Created by lee on 11/25/26.
//
// FTracker/core/src/savings.cpp
#include <pesa/savings.h>
#include "log_sql.h"
#include "db.h"
#include "money_sql.h"
#include "util.h"

namespace pesa {
    namespace {
        constexpr const char *kSelectPot =
                "SELECT p.id, p.name, p.kind, p.is_main, "
                "COALESCE((SELECT SUM(l.amount) FROM ledger l WHERE l.pot_id = p.id), 0) FROM pots p ";

        Pot readPot(const detail::Statement &st) {
            return Pot{
                .id = st.integer(0),
                .name = st.text(1),
                .kind = static_cast<PotKind>(st.integer(2)),
                .isMain = st.integer(3) != 0,
                .balance = st.integer(4),
            };
        }
    } // namespace

    std::string ledgerKindName(LedgerKind kind) {
        switch (kind) {
            case LedgerKind::OpeningBalance: return "Opening balance";
            case LedgerKind::PeriodRemainder: return "Period remainder";
            case LedgerKind::AddedToSavings: return "Added to savings";
            case LedgerKind::MovedToEarned: return "Moved to earned";
            case LedgerKind::ExpenseCover: return "Expense cover";
        }
        return "";
    }

    PotId Savings::addPot(std::string_view rawName, PotKind kind) {
        const std::string name = detail::cleanName(rawName, "A pot name cannot be empty.");
        detail::Transaction tx(db_);
        {
            auto st = db_.prepare("SELECT 1 FROM pots WHERE name = ?"); // the column ignores upper/lower case
            st.bind(1, name);
            if (st.step()) throw Error("A pot named '" + name + "' already exists.");
        }
        db_.prepare("INSERT INTO pots(name, kind, sort_order) "
                    "VALUES (?, ?, COALESCE((SELECT MAX(sort_order) FROM pots), 0) + 1)")
                .bind(1, name)
                .bind(2, static_cast<std::int64_t>(kind))
                .run();
        const PotId id = db_.lastInsertId();
        detail::writeLog(db_, LogKind::Savings, "New pot: " + name);
        tx.commit();
        return id;
    }

    PotId Savings::mainPot() const { return detail::mainPotId(db_); }

    std::optional<Pot> Savings::findPot(PotId id) const {
        auto st = db_.prepare(std::string(kSelectPot) + "WHERE p.id = ?");
        st.bind(1, id);
        if (!st.step()) return std::nullopt;
        return readPot(st);
    }

    std::vector<Pot> Savings::pots() const {
        auto st = db_.prepare(std::string(kSelectPot) + "ORDER BY p.is_main DESC, p.sort_order, p.id");
        std::vector<Pot> out;
        while (st.step()) out.push_back(readPot(st));
        return out;
    }

    Money Savings::total() const {
        auto st = db_.prepare("SELECT COALESCE(SUM(amount), 0) FROM ledger");
        st.step();
        return st.integer(0);
    }

    std::vector<LedgerLine> Savings::ledger() const {
        auto st = db_.prepare("SELECT l.id, l.date, l.pot_id, p.name, l.amount, l.kind, l.note, l.entry_id "
            "FROM ledger l JOIN pots p ON p.id = l.pot_id ORDER BY l.date, l.id");
        std::vector<LedgerLine> out;
        while (st.step()) {
            LedgerLine line;
            line.id = st.integer(0);
            line.date = Date::parse(st.text(1));
            line.potId = st.integer(2);
            line.potName = st.text(3);
            line.amount = st.integer(4);
            line.kind = static_cast<LedgerKind>(st.integer(5));
            line.note = st.text(6);
            if (!st.isNull(7)) line.entryId = st.integer(7);
            out.push_back(std::move(line));
        }
        return out;
    }

    void Savings::add(const Date &date, PotId pot, Money amount, std::string_view note, LedgerKind kind) {
        detail::requirePositive(amount);
        detail::Transaction tx(db_);
        const std::string potName = detail::requirePot(db_, pot);
        detail::insertLedger(db_, date, pot, amount, kind, note);
        detail::writeLog(db_, LogKind::Savings,
                         "Added KES " + formatMoney(amount) + " to " + potName + (note.empty()
                                 ? ""
                                 : ": " + std::string(note)));
        tx.commit();
    }

    void Savings::moveToEarned(const Date &date, PotId pot, Money amount) {
        detail::requirePositive(amount);
        detail::Transaction tx(db_);
        const std::string potName = detail::requirePot(db_, pot);

        PeriodId period = 0;
        {
            auto st = db_.prepare("SELECT id FROM periods WHERE end_date IS NULL");
            if (!st.step()) throw Error("There is no open period. Run 'setup' first.");
            period = st.integer(0);
        }
        detail::insertLedger(db_, date, pot, -amount, LedgerKind::MovedToEarned, "Into this period", period);
        detail::insertIncome(db_, period, date, "From " + potName, amount);
        detail::writeLog(db_, LogKind::Savings, "Moved KES " + formatMoney(amount) + " from " + potName + " to earned");
        tx.commit();
    }
} // namespace pesa
