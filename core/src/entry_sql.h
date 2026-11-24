// FTracker/core/src/entry_sql.h
// FTracker/core/src/entry_sql.h  (PRIVATE)
// SQL shared by Entries and Days.
#pragma once

#include <pesa/entries.h>

#include "db.h"

namespace pesa::detail {

// JOIN pulls in the category's name and sort order.
inline constexpr const char* kSelectEntry =
    "SELECT e.id, e.date, e.item_id, e.category_id, c.name, e.name, e.amount, "
    "e.original_amount, e.kind, e.funded_from "
    "FROM entries e JOIN categories c ON c.id = e.category_id ";

Entry readEntry(const Statement& st);

// Makes sure a row exists for the date. A date is unique, so asking twice is harmless.
void ensureDay(Database& db, const Date& date);

// Saves the sum of the day's counted (not skipped) entries into days.result.
void recalculateDay(Database& db, const Date& date);

} // namespace pesa::detail
