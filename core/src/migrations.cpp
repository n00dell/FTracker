// FTracker/core/src/migrations.cpp
#include "migrations.h"

#include <pesa/common.h>

#include <array>
#include <string>

namespace pesa::detail {

namespace {

struct Migration {
    int         version;
    const char* sql;
};

// RULE: never edit a migration once released. To change the layout, add a new one.
constexpr std::array kMigrations = {
    Migration{1, R"sql(
        CREATE TABLE categories (
            id         INTEGER PRIMARY KEY,
            name       TEXT    NOT NULL UNIQUE COLLATE NOCASE,
            sort_order INTEGER NOT NULL,
            is_active  INTEGER NOT NULL DEFAULT 1 CHECK (is_active IN (0, 1))
        );
    )sql"},

    Migration{2, R"sql(
        CREATE TABLE items (
            id             INTEGER PRIMARY KEY,
            category_id    INTEGER NOT NULL REFERENCES categories(id),
            name           TEXT    NOT NULL COLLATE NOCASE,
            default_amount INTEGER NOT NULL CHECK (default_amount > 0),
            weekdays       INTEGER NOT NULL DEFAULT 127 CHECK (weekdays BETWEEN 0 AND 127),
            is_active      INTEGER NOT NULL DEFAULT 1 CHECK (is_active IN (0, 1)),
            UNIQUE (category_id, name)
        );
    )sql"},

    // Soft delete. The old tables said "name must be unique" for ALL rows, and SQLite
    // cannot remove a constraint in place. So we rebuild: new table, copy rows, drop the
    // old table, rename the new one. The unique rule becomes a "partial unique index"
    // that only applies to rows that are not deleted, which frees a deleted row's name.
    Migration{3, R"sql(
        CREATE TABLE categories_new (
            id         INTEGER PRIMARY KEY,
            name       TEXT    NOT NULL COLLATE NOCASE,
            sort_order INTEGER NOT NULL,
            is_active  INTEGER NOT NULL DEFAULT 1 CHECK (is_active IN (0, 1)),
            is_deleted INTEGER NOT NULL DEFAULT 0 CHECK (is_deleted IN (0, 1))
        );
        INSERT INTO categories_new(id, name, sort_order, is_active)
            SELECT id, name, sort_order, is_active FROM categories;
        DROP TABLE categories;
        ALTER TABLE categories_new RENAME TO categories;
        CREATE UNIQUE INDEX ux_categories_name ON categories(name) WHERE is_deleted = 0;

        CREATE TABLE items_new (
            id             INTEGER PRIMARY KEY,
            category_id    INTEGER NOT NULL REFERENCES categories(id),
            name           TEXT    NOT NULL COLLATE NOCASE,
            default_amount INTEGER NOT NULL CHECK (default_amount > 0),
            weekdays       INTEGER NOT NULL DEFAULT 127 CHECK (weekdays BETWEEN 0 AND 127),
            is_active      INTEGER NOT NULL DEFAULT 1 CHECK (is_active IN (0, 1)),
            is_deleted     INTEGER NOT NULL DEFAULT 0 CHECK (is_deleted IN (0, 1))
        );
        INSERT INTO items_new(id, category_id, name, default_amount, weekdays, is_active)
            SELECT id, category_id, name, default_amount, weekdays, is_active FROM items;
        DROP TABLE items;
        ALTER TABLE items_new RENAME TO items;
        CREATE UNIQUE INDEX ux_items_name ON items(category_id, name) WHERE is_deleted = 0;
    )sql"},

    // Days and entries. A day is one row (the date is the primary key, so it is unique),
    // and every entry points at its day. Nothing is rebuilt here, so no data is touched.
    Migration{4, R"sql(
        CREATE TABLE days (
            date       TEXT    PRIMARY KEY
                       CHECK (date GLOB '[0-9][0-9][0-9][0-9]-[0-9][0-9]-[0-9][0-9]'),
            calculated INTEGER NOT NULL DEFAULT 0 CHECK (calculated IN (0, 1)),
            result     INTEGER                    -- saved total of the day; NULL = recalculate
        );

        CREATE TABLE entries (
            id              INTEGER PRIMARY KEY,
            date            TEXT    NOT NULL REFERENCES days(date),
            item_id         INTEGER REFERENCES items(id),       -- NULL means a one-off
            category_id     INTEGER NOT NULL REFERENCES categories(id),
            name            TEXT    NOT NULL,
            amount          INTEGER NOT NULL CHECK (amount > 0),
            original_amount INTEGER CHECK (original_amount > 0), -- set when an auto entry is edited
            kind            INTEGER NOT NULL CHECK (kind BETWEEN 0 AND 3),
            funded_from     INTEGER NOT NULL DEFAULT 0 CHECK (funded_from IN (0, 1))
        );
        CREATE INDEX idx_entries_date ON entries(date);
    )sql"},
};

// While tables are being rebuilt, foreign keys must be switched off (SQLite ignores
// that switch inside a transaction, so this has to wrap the Transaction). The
// destructor switches them back on, even if the migration throws.
class ForeignKeysOff {
public:
    explicit ForeignKeysOff(Database& db) : db_(db) { db_.exec("PRAGMA foreign_keys = OFF;"); }
    ~ForeignKeysOff() {
        try { db_.exec("PRAGMA foreign_keys = ON;"); } catch (...) {}
    }
    ForeignKeysOff(const ForeignKeysOff&) = delete;
    ForeignKeysOff& operator=(const ForeignKeysOff&) = delete;

private:
    Database& db_;
};

// Because checking was off, verify nothing now points at a missing row.
void requireForeignKeysIntact(Database& db, int version) {
    auto st = db.prepare("PRAGMA foreign_key_check");
    if (st.step()) {
        throw Error("Migration " + std::to_string(version) + " would break the data. Nothing was changed.");
    }
}

} // namespace

int latestSchemaVersion() { return kMigrations.back().version; }

int currentSchemaVersion(Database& db) {
    db.exec("CREATE TABLE IF NOT EXISTS meta (key TEXT PRIMARY KEY, value TEXT NOT NULL);");
    auto st = db.prepare("SELECT value FROM meta WHERE key = 'schema_version'");
    return st.step() ? std::stoi(st.text(0)) : 0;   // no row = brand new file
}

void migrate(Database& db) {
    const int current = currentSchemaVersion(db);

    if (current > latestSchemaVersion()) {
        throw Error("This database was made by a newer Pesa. Please update Pesa.");
    }

    for (const Migration& m : kMigrations) {
        if (m.version <= current) continue;

        ForeignKeysOff fkOff(db);   // declared first, so it is undone last
        Transaction tx(db);         // a failed step leaves the file untouched
        db.exec(m.sql);
        requireForeignKeysIntact(db, m.version);
        db.prepare("INSERT INTO meta(key, value) VALUES ('schema_version', ?) "
                   "ON CONFLICT(key) DO UPDATE SET value = excluded.value")
            .bind(1, std::to_string(m.version))
            .run();
        tx.commit();
    }
}

} // namespace pesa::detail
