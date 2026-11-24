// FTracker/core/src/entry_sql.h
// (PRIVATE) SQL shared by Entries, Days and the daily job.
#pragma once

#include <pesa/entries.h>

#include "db.h"

namespace pesa::detail {

    // LEFT JOIN: a one-off has no category, so the join must keep rows with no match.
    // (A plain JOIN would silently drop every one-off from the day view.)
    inline constexpr const char* kSelectEntry =
        "SELECT e.id, e.date, e.item_id, e.category_id, c.name, e.name, e.amount, "
        "e.original_amount, e.kind, e.funded_from, e.rule_id "
        "FROM entries e LEFT JOIN categories c ON c.id = e.category_id ";

    Entry readEntry(const Statement& st);

    // Makes sure a row exists for the date. A date is unique, so asking twice is harmless.
    void ensureDay(Database& db, const Date& date);

    // Saves the sum of the day's counted (not skipped) entries into days.result.
    void recalculateDay(Database& db, const Date& date);

} // namespace pesa::detail