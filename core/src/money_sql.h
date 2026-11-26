// FTracker/core/src/money_sql.h
// (PRIVATE) SQL shared by Savings, Periods and Entries.
#pragma once

#include <pesa/savings.h>

#include "db.h"

#include <optional>
#include <string>
#include <string_view>

namespace pesa::detail {
    PotId mainPotId(Database & db);

    // Throws if there is no such pot; returns its name.
    std::string requirePot(Database &db, PotId id);

    void insertLedger(Database &db, const Date &date, PotId pot, Money amount, LedgerKind kind,
                      std::string_view note, std::optional<PeriodId> period = std::nullopt);

    void insertIncome(Database &db, PeriodId period, const Date &date, std::string_view label, Money amount);

    // Makes the ledger match one entry: the old line is removed, and if the entry is paid from
    // savings (and not skipped) a fresh line is added. Calling it twice changes nothing.
    void syncExpenseCover(Database &db, EntryId entry);

    // For deleting an entry: removes its ledger line (the refund).
    void dropExpenseCover(Database &db, EntryId entry);
} // namespace pesa::detail