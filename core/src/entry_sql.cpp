// FTracker/core/src/entry_sql.cpp
#include "entry_sql.h"

namespace pesa::detail {

namespace {

std::optional<std::int64_t> optionalInteger(const Statement& st, int column) {
    if (st.isNull(column)) return std::nullopt;
    return st.integer(column);
}

} // namespace

Entry readEntry(const Statement& st) {
    return Entry{
        .id             = st.integer(0),
        .date           = Date::parse(st.text(1)),
        .itemId         = optionalInteger(st, 2),
        .categoryId     = st.integer(3),
        .categoryName   = st.text(4),
        .name           = st.text(5),
        .amount         = st.integer(6),
        .originalAmount = optionalInteger(st, 7),
        .kind           = static_cast<EntryKind>(st.integer(8)),
        .fund           = static_cast<FundedFrom>(st.integer(9)),
    };
}

void ensureDay(Database& db, const Date& date) {
    // "Upsert": insert the day, or quietly do nothing if the date already exists.
    db.prepare("INSERT INTO days(date) VALUES (?) ON CONFLICT(date) DO NOTHING")
        .bind(1, date.toString())
        .run();
}

void recalculateDay(Database& db, const Date& date) {
    // ?1 is used twice: both places get the same date from one bind().
    db.prepare("UPDATE days SET result = "
               "(SELECT COALESCE(SUM(amount), 0) FROM entries WHERE date = ?1 AND kind != 3) "
               "WHERE date = ?1")
        .bind(1, date.toString())
        .run();
}

} // namespace pesa::detail
