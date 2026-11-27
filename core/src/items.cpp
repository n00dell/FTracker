// FTracker/core/src/items.cpp
#include <pesa/items.h>
#include "log_sql.h"
#include "db.h"
#include "util.h"

namespace pesa {
    namespace {
        constexpr const char *kEmptyName = "An item name cannot be empty.";

        // One SELECT used by list() and find(). JOIN pulls in the category's name.
        constexpr const char *kSelectItem =
"SELECT i.id, i.category_id, c.name, i.name, i.default_amount, i.is_active, i.is_deleted, i.expires_on "
        "FROM items i JOIN categories c ON c.id = i.category_id ";

        Item readItem(const detail::Statement& st) {
            Item item{
                .id            = st.integer(0),
                .categoryId    = st.integer(1),
                .categoryName  = st.text(2),
                .name          = st.text(3),
                .defaultAmount = st.integer(4),
                .isActive      = st.integer(5) != 0,
                .isDeleted     = st.integer(6) != 0,
            };
            if (!st.isNull(7)) item.expiresOn = Date::parse(st.text(7));
            return item;
        }

        std::string noItem(ItemId id) { return "No item with id " + std::to_string(id) + "."; }

        // Throws unless the category exists, is not deleted and is active.
        void requireUsableCategory(detail::Database &db, CategoryId id) {
            auto st = db.prepare("SELECT is_active FROM categories WHERE id = ? AND is_deleted = 0");
            st.bind(1, id);
            if (!st.step()) throw Error("No category with id " + std::to_string(id) + ".");
            if (st.integer(0) == 0) throw Error("That category is inactive. Activate it first.");
        }

        // Category of an item that is not deleted; throws if there is no such item.
        CategoryId categoryOfItem(detail::Database &db, ItemId id) {
            auto st = db.prepare("SELECT category_id FROM items WHERE id = ? AND is_deleted = 0");
            st.bind(1, id);
            if (!st.step()) throw Error(noItem(id));
            return st.integer(0);
        }

        // Names only have to be unique inside one category, among items NOT deleted.
        bool nameTaken(detail::Database &db, CategoryId categoryId, const std::string &name, ItemId exceptId) {
            auto st = db.prepare(
                "SELECT 1 FROM items WHERE category_id = ? AND name = ? AND id != ? AND is_deleted = 0");
            st.bind(1, categoryId).bind(2, name).bind(3, exceptId);
            return st.step();
        }
    } // namespace

    ItemId Items::add(const NewItem &item) {
        const std::string name = detail::cleanName(item.name, kEmptyName);
        detail::requirePositive(item.defaultAmount);
        requireUsableCategory(db_, item.categoryId);
        if (nameTaken(db_, item.categoryId, name, 0)) {
            throw Error("'" + name + "' already exists in that category.");
        }

        // in Items::add, replace the INSERT statement (and the return) with:
        auto insert = db_.prepare("INSERT INTO items(category_id, name, default_amount, expires_on) VALUES (?, ?, ?, ?)");
        insert.bind(1, item.categoryId).bind(2, name).bind(3, item.defaultAmount);
        if (item.expiresOn) insert.bind(4, item.expiresOn->toString()); else insert.bindNull(4);
        insert.run();
        return db_.lastInsertId();
    }

    void Items::rename(ItemId id, std::string_view newName) {
        const std::string name = detail::cleanName(newName, kEmptyName);
        const CategoryId categoryId = categoryOfItem(db_, id); // also proves the item exists
        if (nameTaken(db_, categoryId, name, id)) {
            throw Error("'" + name + "' already exists in that category.");
        }
        db_.prepare("UPDATE items SET name = ? WHERE id = ?").bind(1, name).bind(2, id).run();
    }

    void Items::setDefaultAmount(ItemId id, Money amount) {
        detail::requirePositive(amount);
        db_.prepare("UPDATE items SET default_amount = ? WHERE id = ? AND is_deleted = 0")
                .bind(1, amount).bind(2, id).run();
        if (db_.changes() == 0) throw Error(noItem(id));
    }

    void Items::setActive(ItemId id, bool active) {
        db_.prepare(active ? "UPDATE items SET is_active = 1, expires_on = NULL WHERE id = ? AND is_deleted = 0"
                           : "UPDATE items SET is_active = 0 WHERE id = ? AND is_deleted = 0")
            .bind(1, id).run();
        if (db_.changes() == 0) throw Error(noItem(id));
    }

    void Items::setExpiry(ItemId id, std::optional<Date> lastDay) {
        auto st = db_.prepare("UPDATE items SET expires_on = ? WHERE id = ? AND is_deleted = 0");
        if (lastDay) st.bind(1, lastDay->toString()); else st.bindNull(1);
        st.bind(2, id);
        st.run();
        if (db_.changes() == 0) throw Error(noItem(id));
    }

    std::vector<std::string> Items::pauseExpired(const Date& today) {
        detail::Transaction tx(db_);
        const char* const where = "is_deleted = 0 AND is_active = 1 AND expires_on IS NOT NULL AND expires_on < ?";
        std::vector<std::string> names;
        {
            auto st = db_.prepare(std::string("SELECT name FROM items WHERE ") + where + " ORDER BY id");
            st.bind(1, today.toString());
            while (st.step()) names.push_back(st.text(0));
        }
        if (!names.empty()) {
            db_.prepare(std::string("UPDATE items SET is_active = 0 WHERE ") + where).bind(1, today.toString()).run();
            std::string joined;
            for (const std::string& name : names) joined += (joined.empty() ? "" : ", ") + name;
            detail::writeLog(db_, LogKind::Edit, "Short-lived items paused (their last day passed): " + joined);
        }
        tx.commit();
        return names;
    }

    void Items::remove(ItemId id) {
        db_.prepare("UPDATE items SET is_deleted = 1 WHERE id = ? AND is_deleted = 0")
                .bind(1, id).run();
        if (db_.changes() == 0) throw Error(noItem(id));
    }

    void Items::restore(ItemId id) {
        CategoryId categoryId = 0;
        std::string name;
        {
            auto st = db_.prepare(
                "SELECT i.category_id, i.name, c.is_deleted, c.name FROM items i "
                "JOIN categories c ON c.id = i.category_id WHERE i.id = ? AND i.is_deleted = 1");
            st.bind(1, id);
            if (!st.step()) throw Error("No deleted item with id " + std::to_string(id) + ".");
            if (st.integer(2) != 0) {
                throw Error("Restore the category '" + st.text(3) + "' first.");
            }
            categoryId = st.integer(0);
            name = st.text(1);
        }
        if (nameTaken(db_, categoryId, name, id)) {
            throw Error("Cannot restore: another item in that category is now called '" + name +
                        "'. Rename it first.");
        }
        db_.prepare("UPDATE items SET is_deleted = 0 WHERE id = ?").bind(1, id).run();
    }

    std::optional<Item> Items::find(ItemId id) const {
        auto st = db_.prepare(std::string(kSelectItem) + "WHERE i.id = ?");
        st.bind(1, id);
        if (!st.step()) return std::nullopt;
        return readItem(st);
    }

    std::vector<Item> Items::list(std::optional<CategoryId> category, Visibility show) const {
        // Build the SQL piece by piece; each "?" is bound below in the same order.
        std::string sql = std::string(kSelectItem) + "WHERE ";
        if (show == Visibility::ActiveOnly) {
            sql += "i.is_deleted = 0 AND i.is_active = 1 AND c.is_deleted = 0 AND c.is_active = 1 ";
        } else if (show == Visibility::IncludeInactive) {
            sql += "i.is_deleted = 0 AND c.is_deleted = 0 ";
        } else {
            sql += "i.is_deleted = 1 "; // items of a deleted category come back with the category
        }
        if (category) sql += "AND i.category_id = ? ";
        sql += "ORDER BY c.is_system, c.sort_order, c.id, i.id";

        auto st = db_.prepare(sql);
        if (category) st.bind(1, *category);

        std::vector<Item> out;
        while (st.step()) out.push_back(readItem(st));
        return out;
    }
} // namespace pesa
