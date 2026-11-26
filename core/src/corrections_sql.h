// FTracker/core/src/corrections_sql.h
// (PRIVATE) The shared machinery of bulk changes: finding matching entries, restore points,
// and the amount / skip lookups the daily job uses.
#pragma once

#include <pesa/corrections.h>

#include "db.h"

#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace pesa::detail {
    // Fills the ?1, ?2, ... placeholders of a statement. Lets one WHERE text serve several statements.
    using Binder = std::function<void(Statement &)>;

    struct Matched {
        int entries = 0;
        int days = 0;
        Money total = 0;
        std::vector<ChangedEntry> sample; // the first five, oldest first
    };

    // A preview does the real work and rolls it back. Inside someone else's transaction nothing
    // would be rolled back, so that is refused.
    void requireOwnTransactionForPreview(Database &db, Mode mode);

    // `where` is SQL after "FROM entries WHERE", written with UNQUALIFIED column names
    // (kind, date, item_id ...) so the same text works for SELECT, UPDATE and DELETE.
    Matched match(Database &db, const std::string &where, const Binder &bind);

    int countMatching(Database &db, const std::string &where, const Binder &bind);

    std::vector<EntryId> idsMatching(Database &db, const std::string &where, const Binder &bind);

    // Rebuilds days.result for every day in the range.
    void recalculateRange(Database &db, const Date &from, const Date &to);

    // Snapshots the entries in the range, and the state of every rule and skip. The newest 5
    // restore points are kept (pass prune = false to postpone that, as restoring does).
    RestorePointId saveRestorePoint(Database &db, std::string_view label, const Date &from, const Date &to,
                                    bool prune = true);

    void pruneRestorePoints(Database &db);

    // What an item posts on a date: its default plus every active adjust rule that covers the date.
    Money postAmount(Database &db, const Date &date, ItemId item);

    struct ActiveSkip {
        SkipId id = 0;
        std::string name;
    };

    // The skip (if any) that covers this item on this date.
    std::optional<ActiveSkip> activeSkip(Database &db, const Date &date, ItemId item);

    struct Unskipped {
        int entries = 0;
        int days = 0;
        Money total = 0;
    };

    // Brings back every grey entry that belongs to `skip`. Call it AFTER the skip is marked deleted.
    // An entry that another skip still covers stays grey, under that other skip.
    Unskipped unskipMarkers(Database &db, SkipId skip);
} // namespace pesa::detail
