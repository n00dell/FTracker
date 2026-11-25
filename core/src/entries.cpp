// FTracker/core/src/entries.cpp
#include <pesa/entries.h>
#include "money_sql.h"
#include "db.h"
#include "entry_sql.h"
#include "util.h"

namespace pesa {

namespace {

constexpr const char* kEmptyName = "An entry name cannot be empty.";

std::string noEntry(EntryId id) { return "No entry with id " + std::to_string(id) + "."; }

struct UsableItem {
    std::string name;
    CategoryId  categoryId;
    Money       defaultAmount;
};

// Only items you could pick on the dashboard: not deleted, not paused, and the same
// for their category.
UsableItem loadUsableItem(detail::Database& db, ItemId id) {
    auto st = db.prepare(
        "SELECT i.name, i.category_id, i.default_amount FROM items i "
        "JOIN categories c ON c.id = i.category_id "
        "WHERE i.id = ? AND i.is_deleted = 0 AND i.is_active = 1 "
        "AND c.is_deleted = 0 AND c.is_active = 1");
    st.bind(1, id);
    if (!st.step()) {
        throw Error("Item " + std::to_string(id) + " is not available (missing, paused or deleted).");
    }
    return UsableItem{st.text(0), st.integer(1), st.integer(2)};
}

void requireUsableCategory(detail::Database& db, CategoryId id) {
    auto st = db.prepare("SELECT 1 FROM categories WHERE id = ? AND is_deleted = 0 AND is_active = 1");
    st.bind(1, id);
    if (!st.step()) {
        throw Error("Category " + std::to_string(id) + " is not available (missing, paused or deleted).");
    }
}

} // namespace

EntryId Entries::addFromItem(const Date& date, ItemId itemId, std::optional<Money> amount,
                             FundedFrom fund) {
    if (amount) detail::requirePositive(*amount);

    detail::Transaction tx(db_);
    const UsableItem item = loadUsableItem(db_, itemId);

    detail::ensureDay(db_, date);
    db_.prepare("INSERT INTO entries(date, item_id, category_id, name, amount, kind, funded_from) "
                "VALUES (?, ?, ?, ?, ?, ?, ?)")
        .bind(1, date.toString())
        .bind(2, itemId)
        .bind(3, item.categoryId)
        .bind(4, item.name)
        .bind(5, amount.value_or(item.defaultAmount))
        .bind(6, static_cast<std::int64_t>(EntryKind::Added))
        .bind(7, static_cast<std::int64_t>(fund))
        .run();
    const EntryId id = db_.lastInsertId();   // read it straight after the INSERT
    detail::syncExpenseCover(db_, id);

    detail::recalculateDay(db_, date);
    tx.commit();
    return id;
}

    EntryId Entries::addOneOff(const Date& date, std::string_view rawName, Money amount, FundedFrom fund) {
    const std::string name = detail::cleanName(rawName, kEmptyName);
    detail::requirePositive(amount);

    detail::Transaction tx(db_);
    detail::ensureDay(db_, date);
    // No item_id and no category_id: both stay NULL, which is what marks a one-off.
    db_.prepare("INSERT INTO entries(date, name, amount, kind, funded_from) VALUES (?, ?, ?, ?, ?)")
        .bind(1, date.toString())
        .bind(2, name)
        .bind(3, amount)
        .bind(4, static_cast<std::int64_t>(EntryKind::Added))
        .bind(5, static_cast<std::int64_t>(fund))
        .run();
    const EntryId id = db_.lastInsertId();

    detail::syncExpenseCover(db_, id);
    detail::recalculateDay(db_, date);
    tx.commit();
    return id;
}

void Entries::setAmount(EntryId id, Money amount) {
    detail::requirePositive(amount);

    detail::Transaction tx(db_);
    const auto entry = find(id);
    if (!entry) throw Error(noEntry(id));
    if (entry->kind == EntryKind::Skipped) throw Error("A skipped entry cannot be edited.");
    if (entry->amount == amount) return;   // nothing changes, so it is not "edited"

    if (entry->kind == EntryKind::AutoPosted) {
        // First edit of an auto entry: remember what the rule posted.
        db_.prepare("UPDATE entries SET amount = ?, original_amount = ?, kind = ? WHERE id = ?")
            .bind(1, amount)
            .bind(2, entry->amount)
            .bind(3, static_cast<std::int64_t>(EntryKind::Edited))
            .bind(4, id)
            .run();
    } else {
        // Added stays Added. Edited stays Edited and keeps its first original amount.
        db_.prepare("UPDATE entries SET amount = ? WHERE id = ?").bind(1, amount).bind(2, id).run();
    }
    detail::syncExpenseCover(db_, id);
    detail::recalculateDay(db_, entry->date);
    tx.commit();
}

void Entries::setFundedFrom(EntryId id, FundedFrom fund) {
    detail::Transaction tx(db_);
    const auto entry = find(id);
    if (!entry) throw Error(noEntry(id));
    if (entry->kind == EntryKind::Skipped) throw Error("A skipped entry cannot be edited.");

    // Moving where the money comes from is not an amount edit, so the kind is unchanged.
    db_.prepare("UPDATE entries SET funded_from = ? WHERE id = ?")
        .bind(1, static_cast<std::int64_t>(fund))
        .bind(2, id)
        .run();
    detail::syncExpenseCover(db_, id);
    tx.commit();   // the day's saved total does not depend on funding, so no recalculation
}

void Entries::remove(EntryId id) {
    detail::Transaction tx(db_);
    const auto entry = find(id);
    if (!entry) throw Error(noEntry(id));

    detail::dropExpenseCover(db_, id);

    db_.prepare("DELETE FROM entries WHERE id = ?").bind(1, id).run();
    detail::recalculateDay(db_, entry->date);

    tx.commit();
}

    Money Entries::coverFromSavings(std::span<const EntryId> ids) {
    detail::Transaction tx(db_);
    Money total = 0;
    for (const EntryId id : ids) {
        const auto entry = find(id);
        if (!entry) throw Error(noEntry(id));
        if (entry->fund == FundedFrom::Savings) continue;   // already covered
        setFundedFrom(id, FundedFrom::Savings);             // joins this transaction
        total += entry->amount;
    }
    tx.commit();
    return total;
}

std::optional<Entry> Entries::find(EntryId id) const {
    auto st = db_.prepare(std::string(detail::kSelectEntry) + "WHERE e.id = ?");
    st.bind(1, id);
    if (!st.step()) return std::nullopt;
    return detail::readEntry(st);
}

} // namespace pesa
