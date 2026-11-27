// FTracker/core/src/categories.cpp
#include <pesa/categories.h>
#include  "log_sql.h"
#include "db.h"
#include "util.h"

namespace pesa {
    namespace {
        constexpr const char *kEmptyName = "A category name cannot be empty.";
        constexpr const char *kSelect =
                "SELECT id, name, sort_order, is_active, is_deleted, is_system, expires_on FROM categories ";

        Category readCategory(const detail::Statement& st) {
            Category c{
                .id        = st.integer(0),
                .name      = st.text(1),
                .sortOrder = static_cast<int>(st.integer(2)),
                .isActive  = st.integer(3) != 0,
                .isDeleted = st.integer(4) != 0,
                .isSystem  = st.integer(5) != 0,
            };
            if (!st.isNull(6)) c.expiresOn = Date::parse(st.text(6));
            return c;
        }

        // Names only have to be unique among categories that are NOT deleted.
        bool nameTaken(detail::Database &db, const std::string &name, CategoryId exceptId) {
            auto st = db.prepare("SELECT 1 FROM categories WHERE name = ? AND id != ? AND is_deleted = 0");
            st.bind(1, name).bind(2, exceptId);
            return st.step();
        }

        std::string noCategory(CategoryId id) { return "No category with id " + std::to_string(id) + "."; }

        // Misc is built in. Call this before rename / pause / delete.
        void requireNotSystem(detail::Database &db, CategoryId id) {
            auto st = db.prepare("SELECT name FROM categories WHERE id = ? AND is_system = 1");
            st.bind(1, id);
            if (st.step()) throw Error("'" + st.text(0) + "' is built in and cannot be renamed, paused or deleted.");
        }

        // The name of a DELETED category, or throws if there is no such deleted category.
        std::string deletedName(detail::Database &db, CategoryId id) {
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
        requireNotSystem(db_, id);
        if (nameTaken(db_, name, id)) {
            throw Error("A category named '" + name + "' already exists.");
        }
        db_.prepare("UPDATE categories SET name = ? WHERE id = ? AND is_deleted = 0")
                .bind(1, name).bind(2, id).run();
        if (db_.changes() == 0) throw Error(noCategory(id));
    }

    void Categories::setActive(CategoryId id, bool active) {
        requireNotSystem(db_, id);
        // Resuming also makes it permanent: its end date has done its job.
        db_.prepare(active ? "UPDATE categories SET is_active = 1, expires_on = NULL WHERE id = ? AND is_deleted = 0"
                           : "UPDATE categories SET is_active = 0 WHERE id = ? AND is_deleted = 0")
            .bind(1, id).run();
        if (db_.changes() == 0) throw Error(noCategory(id));
    }

    void Categories::setExpiry(CategoryId id, std::optional<Date> lastDay) {
        requireNotSystem(db_, id);
        auto st = db_.prepare("UPDATE categories SET expires_on = ? WHERE id = ? AND is_deleted = 0");
        if (lastDay) st.bind(1, lastDay->toString()); else st.bindNull(1);
        st.bind(2, id);
        st.run();
        if (db_.changes() == 0) throw Error(noCategory(id));
    }

    std::vector<std::string> Categories::pauseExpired(const Date& today) {
        detail::Transaction tx(db_);
        const char* const where = "is_deleted = 0 AND is_active = 1 AND is_system = 0 "
                                  "AND expires_on IS NOT NULL AND expires_on < ?";
        std::vector<std::string> names;
        {
            auto st = db_.prepare(std::string("SELECT name FROM categories WHERE ") + where + " ORDER BY id");
            st.bind(1, today.toString());
            while (st.step()) names.push_back(st.text(0));
        }
        if (!names.empty()) {
            db_.prepare(std::string("UPDATE categories SET is_active = 0 WHERE ") + where).bind(1, today.toString()).run();
            std::string joined;
            for (const std::string& name : names) joined += (joined.empty() ? "" : ", ") + name;
            detail::writeLog(db_, LogKind::Edit, "Short-lived categories paused (their last day passed): " + joined);
        }
        tx.commit();
        return names;
    }

    void Categories::remove(CategoryId id) {
        requireNotSystem(db_, id);
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

    CategoryId Categories::misc() const {
        auto st = db_.prepare("SELECT id FROM categories WHERE is_system = 1 AND is_deleted = 0");
        if (!st.step()) throw Error("The built-in Misc category is missing.");
        return st.integer(0);
    }

    std::optional<Category> Categories::find(CategoryId id) const {
        auto st = db_.prepare(std::string(kSelect) + "WHERE id = ?");
        st.bind(1, id);
        if (!st.step()) return std::nullopt;
        return readCategory(st);
    }

    std::vector<Category> Categories::list(Visibility show) const {
        std::string sql = std::string(kSelect) + "WHERE ";
        if (show == Visibility::ActiveOnly) sql += "is_deleted = 0 AND is_active = 1 ";
        else if (show == Visibility::IncludeInactive) sql += "is_deleted = 0 ";
        else sql += "is_deleted = 1 ";
        sql += "ORDER BY is_system, sort_order, id"; // Misc always comes last

        auto st = db_.prepare(sql);
        std::vector<Category> out;
        while (st.step()) out.push_back(readCategory(st));
        return out;
    }
} // namespace pesa
