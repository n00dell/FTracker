// FTracker/core/src/categories.cpp
#include <pesa/categories.h>

#include "db.h"
#include "util.h"

namespace pesa {

namespace {

constexpr const char* kEmptyName = "A category name cannot be empty.";
constexpr const char* kSelect = "SELECT id, name, sort_order, is_active, is_deleted FROM categories ";

Category readCategory(const detail::Statement& st) {
    return Category{
        .id        = st.integer(0),
        .name      = st.text(1),
        .sortOrder = static_cast<int>(st.integer(2)),
        .isActive  = st.integer(3) != 0,
        .isDeleted = st.integer(4) != 0,
    };
}

// Names only have to be unique among categories that are NOT deleted.
bool nameTaken(detail::Database& db, const std::string& name, CategoryId exceptId) {
    auto st = db.prepare("SELECT 1 FROM categories WHERE name = ? AND id != ? AND is_deleted = 0");
    st.bind(1, name).bind(2, exceptId);
    return st.step();
}

std::string noCategory(CategoryId id) { return "No category with id " + std::to_string(id) + "."; }

// The name of a DELETED category, or throws if there is no such deleted category.
std::string deletedName(detail::Database& db, CategoryId id) {
    auto st = db.prepare("SELECT name FROM categories WHERE id = ? AND is_deleted = 1");
    st.bind(1, id);
    if (!st.step()) throw Error("No deleted category with id " + std::to_string(id) + ".");
    return st.text(0);
}

} // namespace

CategoryId Categories::add(std::string_view rawName) {
    const std::string name = detail::cleanName(rawName, kEmptyName);
    if (nameTaken(db_, name, 0)) {
        throw Error("A category named '" + name + "' already exists.");
    }
    // New categories go to the end of the list.
    db_.prepare("INSERT INTO categories(name, sort_order) "
                "VALUES (?, COALESCE((SELECT MAX(sort_order) FROM categories), 0) + 1)")
        .bind(1, name)
        .run();
    return db_.lastInsertId();
}

void Categories::rename(CategoryId id, std::string_view newName) {
    const std::string name = detail::cleanName(newName, kEmptyName);
    if (nameTaken(db_, name, id)) {
        throw Error("A category named '" + name + "' already exists.");
    }
    db_.prepare("UPDATE categories SET name = ? WHERE id = ? AND is_deleted = 0")
        .bind(1, name).bind(2, id).run();
    if (db_.changes() == 0) throw Error(noCategory(id));
}

void Categories::setActive(CategoryId id, bool active) {
    db_.prepare("UPDATE categories SET is_active = ? WHERE id = ? AND is_deleted = 0")
        .bind(1, active ? 1 : 0).bind(2, id).run();
    if (db_.changes() == 0) throw Error(noCategory(id));
}

void Categories::remove(CategoryId id) {
    db_.prepare("UPDATE categories SET is_deleted = 1 WHERE id = ? AND is_deleted = 0")
        .bind(1, id).run();
    if (db_.changes() == 0) throw Error(noCategory(id));
}

void Categories::restore(CategoryId id) {
    const std::string name = deletedName(db_, id);
    if (nameTaken(db_, name, id)) {
        throw Error("Cannot restore: another category is now called '" + name + "'. Rename it first.");
    }
    db_.prepare("UPDATE categories SET is_deleted = 0 WHERE id = ?").bind(1, id).run();
}

std::optional<Category> Categories::find(CategoryId id) const {
    auto st = db_.prepare(std::string(kSelect) + "WHERE id = ?");
    st.bind(1, id);
    if (!st.step()) return std::nullopt;
    return readCategory(st);
}

std::vector<Category> Categories::list(Visibility show) const {
    std::string sql = std::string(kSelect) + "WHERE ";
    if (show == Visibility::ActiveOnly)            sql += "is_deleted = 0 AND is_active = 1 ";
    else if (show == Visibility::IncludeInactive)  sql += "is_deleted = 0 ";
    else                                           sql += "is_deleted = 1 ";
    sql += "ORDER BY sort_order, id";

    auto st = db_.prepare(sql);
    std::vector<Category> out;
    while (st.step()) out.push_back(readCategory(st));
    return out;
}

} // namespace pesa
