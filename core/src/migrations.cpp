// FTracker/core/src/migrations.cpp
#include "migrations.h"

#include <pesa/common.h>

#include <array>
#include <string>

namespace pesa::detail {
    namespace {
        struct Migration {
            int version;
            const char *sql;
        };

        // RULE: never edit a migration once released. To change the layout, add a new one.
        constexpr std::array kMigrations = {
            Migration{
                1, R"sql(
        CREATE TABLE categories (
            id         INTEGER PRIMARY KEY,
            name       TEXT    NOT NULL UNIQUE COLLATE NOCASE,
            sort_order INTEGER NOT NULL,
            is_active  INTEGER NOT NULL DEFAULT 1 CHECK (is_active IN (0, 1))
        );
    )sql"
            },

            Migration{
                2, R"sql(
        CREATE TABLE items (
            id             INTEGER PRIMARY KEY,
            category_id    INTEGER NOT NULL REFERENCES categories(id),
            name           TEXT    NOT NULL COLLATE NOCASE,
            default_amount INTEGER NOT NULL CHECK (default_amount > 0),
            weekdays       INTEGER NOT NULL DEFAULT 127 CHECK (weekdays BETWEEN 0 AND 127),
            is_active      INTEGER NOT NULL DEFAULT 1 CHECK (is_active IN (0, 1)),
            UNIQUE (category_id, name)
        );
    )sql"
            },

            // Soft delete. The old tables said "name must be unique" for ALL rows, and SQLite
            // cannot remove a constraint in place. So we rebuild: new table, copy rows, drop the
            // old table, rename the new one. The unique rule becomes a "partial unique index"
            // that only applies to rows that are not deleted, which frees a deleted row's name.
            Migration{
                3, R"sql(
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
    )sql"
            },

            // Days and entries. A day is one row (the date is the primary key, so it is unique),
            // and every entry points at its day. Nothing is rebuilt here, so no data is touched.
            Migration{
                4, R"sql(
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
    )sql"
            },

            // Three changes that belong together:
            //  1. "Misc": a built-in category (is_system = 1) that can never be renamed, paused or
            //     deleted, so every item and one-off always has somewhere to live.
            //  2. A settings table (first-run done?, expected pay, auto-post on/off and time).
            //  3. The days an item posts on MOVE to rules (migration 6), so the items table loses
            //     its weekdays column. SQLite cannot reliably drop a column that has a CHECK on it
            //     in older versions, so we rebuild the table the same way migration 3 did.
            Migration{
                5, R"sql(
        ALTER TABLE categories
            ADD COLUMN is_system INTEGER NOT NULL DEFAULT 0 CHECK (is_system IN (0, 1));

        -- If you already made a category called Misc, adopt it instead of making a clash.
        UPDATE categories SET is_system = 1 WHERE name = 'Misc' AND is_deleted = 0;

        INSERT INTO categories(name, sort_order, is_system)
            SELECT 'Misc', COALESCE((SELECT MAX(sort_order) FROM categories), 0) + 1, 1
            WHERE NOT EXISTS (SELECT 1 FROM categories WHERE is_system = 1);

        CREATE TABLE settings (
            key   TEXT PRIMARY KEY,
            value TEXT NOT NULL
        );

        CREATE TABLE items_new (
            id             INTEGER PRIMARY KEY,
            category_id    INTEGER NOT NULL REFERENCES categories(id),
            name           TEXT    NOT NULL COLLATE NOCASE,
            default_amount INTEGER NOT NULL CHECK (default_amount > 0),
            is_active      INTEGER NOT NULL DEFAULT 1 CHECK (is_active IN (0, 1)),
            is_deleted     INTEGER NOT NULL DEFAULT 0 CHECK (is_deleted IN (0, 1))
        );
        INSERT INTO items_new(id, category_id, name, default_amount, is_active, is_deleted)
            SELECT id, category_id, name, default_amount, is_active, is_deleted FROM items;
        DROP TABLE items;
        ALTER TABLE items_new RENAME TO items;
        CREATE UNIQUE INDEX ux_items_name ON items(category_id, name) WHERE is_deleted = 0;
    )sql"
            },

            // Rules (charge rules for now: "post these items on these days"). A rule says WHEN,
            // an item says WHAT and HOW MUCH, and rule_items links the two (many to many).
            // Skip and adjust rules come later as extra columns/tables, so nothing is wasted here.
            Migration{
                6, R"sql(
        CREATE TABLE rules (
            id             INTEGER PRIMARY KEY,
            name           TEXT    NOT NULL COLLATE NOCASE,
            repeat_kind    INTEGER NOT NULL CHECK (repeat_kind IN (0, 1)),   -- 0 weekly, 1 monthly
            weekdays       INTEGER NOT NULL DEFAULT 0 CHECK (weekdays BETWEEN 0 AND 127),
            month_day      INTEGER NOT NULL DEFAULT 0 CHECK (month_day BETWEEN 0 AND 31),
            effective_from TEXT    NOT NULL
                           CHECK (effective_from GLOB '[0-9][0-9][0-9][0-9]-[0-9][0-9]-[0-9][0-9]'),
            effective_to   TEXT    -- last day it applies; NULL = never ends
                           CHECK (effective_to IS NULL OR
                                  effective_to GLOB '[0-9][0-9][0-9][0-9]-[0-9][0-9]-[0-9][0-9]'),
            is_active      INTEGER NOT NULL DEFAULT 1 CHECK (is_active IN (0, 1)),
            is_deleted     INTEGER NOT NULL DEFAULT 0 CHECK (is_deleted IN (0, 1)),
            -- A weekly rule needs at least one weekday; a monthly rule needs a day number.
            CHECK ((repeat_kind = 0 AND weekdays != 0 AND month_day = 0) OR
                   (repeat_kind = 1 AND weekdays = 0 AND month_day BETWEEN 1 AND 31)),
            CHECK (effective_to IS NULL OR effective_to >= effective_from)
        );
        CREATE UNIQUE INDEX ux_rules_name ON rules(name) WHERE is_deleted = 0;

        CREATE TABLE rule_items (
            rule_id INTEGER NOT NULL REFERENCES rules(id),
            item_id INTEGER NOT NULL REFERENCES items(id),
            PRIMARY KEY (rule_id, item_id)
        );
    )sql"
            },
            // Three changes that belong together:
            //  1. A one-off has NO category any more (it was stored under Misc by mistake), so
            //     entries.category_id may now be NULL. SQLite cannot drop a NOT NULL, so we rebuild
            //     the table the same way as migration 3.
            //  2. entries.rule_id: which rule posted an auto entry. With the unique index below it is
            //     the hard guard "a rule posts once per item per day", even if the job ran twice.
            //  3. An issues table where the daily job records a problem instead of hiding it.
            Migration{
                7, R"sql(
        CREATE TABLE entries_new (
            id              INTEGER PRIMARY KEY,
            date            TEXT    NOT NULL REFERENCES days(date),
            item_id         INTEGER REFERENCES items(id),        -- NULL = a one-off
            category_id     INTEGER REFERENCES categories(id),   -- NULL = a one-off
            name            TEXT    NOT NULL,
            amount          INTEGER NOT NULL CHECK (amount > 0),
            original_amount INTEGER CHECK (original_amount > 0),
            kind            INTEGER NOT NULL CHECK (kind BETWEEN 0 AND 3),
            funded_from     INTEGER NOT NULL DEFAULT 0 CHECK (funded_from IN (0, 1)),
            rule_id         INTEGER REFERENCES rules(id),        -- NULL = not posted by a rule
            -- Catalog entries have both an item and a category; one-offs have neither.
            CHECK ((item_id IS NULL AND category_id IS NULL) OR
                   (item_id IS NOT NULL AND category_id IS NOT NULL))
        );
        INSERT INTO entries_new(id, date, item_id, category_id, name, amount,
                                original_amount, kind, funded_from)
            SELECT id, date, item_id,
                   CASE WHEN item_id IS NULL THEN NULL ELSE category_id END,
                   name, amount, original_amount, kind, funded_from
            FROM entries;
        DROP TABLE entries;
        ALTER TABLE entries_new RENAME TO entries;
        CREATE INDEX idx_entries_date ON entries(date);
        CREATE UNIQUE INDEX ux_entries_rule_post ON entries(date, rule_id, item_id)
            WHERE rule_id IS NOT NULL;

        CREATE TABLE issues (
            id         INTEGER PRIMARY KEY,
            day        TEXT NOT NULL,
            message    TEXT NOT NULL,
            created_at TEXT NOT NULL DEFAULT (datetime('now'))
        );
    )sql"
            },
            // Money: pots and the ledger (savings), periods and income (earned).
            //  - Savings is STORED as ledger lines; a pot's balance is the sum of its lines.
            //  - A period runs from one "I got paid" to the next: [start_date, end_date).
            //    The unique index allows only ONE open period (end_date empty) at a time.
            //  - A ledger line may point at the entry it pays for (entry_id), so changing or
            //    deleting that entry can fix the pot ("Expense cover").
            Migration{
                8, R"sql(
        CREATE TABLE pots (
            id         INTEGER PRIMARY KEY,
            name       TEXT    NOT NULL COLLATE NOCASE,
            kind       INTEGER NOT NULL DEFAULT 0 CHECK (kind BETWEEN 0 AND 2),   -- 0 open, 1 locked, 2 tied
            is_main    INTEGER NOT NULL DEFAULT 0 CHECK (is_main IN (0, 1)),
            sort_order INTEGER NOT NULL
        );
        CREATE UNIQUE INDEX ux_pots_name ON pots(name);
        CREATE UNIQUE INDEX ux_pots_one_main ON pots(is_main) WHERE is_main = 1;
        INSERT INTO pots(name, kind, is_main, sort_order) VALUES ('Main savings', 0, 1, 1);

        CREATE TABLE periods (
            id              INTEGER PRIMARY KEY,
            start_date      TEXT NOT NULL
                            CHECK (start_date GLOB '[0-9][0-9][0-9][0-9]-[0-9][0-9]-[0-9][0-9]'),
            end_date        TEXT   -- the day pay arrived; NULL = this is the open period
                            CHECK (end_date IS NULL OR
                                   end_date GLOB '[0-9][0-9][0-9][0-9]-[0-9][0-9]-[0-9][0-9]'),
            expected_payday TEXT
                            CHECK (expected_payday IS NULL OR
                                   expected_payday GLOB '[0-9][0-9][0-9][0-9]-[0-9][0-9]-[0-9][0-9]'),
            remainder       INTEGER,   -- income minus earned spending, set when the period closes
            CHECK (end_date IS NULL OR end_date > start_date)
        );
        CREATE UNIQUE INDEX ux_periods_one_open ON periods((end_date IS NULL)) WHERE end_date IS NULL;

        CREATE TABLE income (
            id        INTEGER PRIMARY KEY,
            period_id INTEGER NOT NULL REFERENCES periods(id),
            date      TEXT    NOT NULL
                      CHECK (date GLOB '[0-9][0-9][0-9][0-9]-[0-9][0-9]-[0-9][0-9]'),
            label     TEXT    NOT NULL,
            amount    INTEGER NOT NULL CHECK (amount > 0)
        );
        CREATE INDEX idx_income_period ON income(period_id);

        CREATE TABLE ledger (
            id        INTEGER PRIMARY KEY,
            date      TEXT    NOT NULL
                      CHECK (date GLOB '[0-9][0-9][0-9][0-9]-[0-9][0-9]-[0-9][0-9]'),
            pot_id    INTEGER NOT NULL REFERENCES pots(id),
            amount    INTEGER NOT NULL CHECK (amount != 0),   -- + into the pot, - out of it
            kind      INTEGER NOT NULL,                       -- see LedgerKind
            note      TEXT    NOT NULL DEFAULT '',
            entry_id  INTEGER UNIQUE REFERENCES entries(id),  -- the entry this line pays for
            period_id INTEGER REFERENCES periods(id)
        );
        CREATE INDEX idx_ledger_pot ON ledger(pot_id);

        -- Entries already paid from savings before this migration get their ledger line now.
        INSERT INTO ledger(date, pot_id, amount, kind, note, entry_id)
            SELECT e.date, (SELECT id FROM pots WHERE is_main = 1), -e.amount, 4, e.name, e.id
            FROM entries e WHERE e.funded_from = 1 AND e.kind != 3;
    )sql"
            },
            // The log: every event, in order. Problems are log lines of kind 5 (Issue), so the
            // old issues table is folded into it. The timestamp comes from SQLite's local clock.
            Migration{
                9, R"sql(
        CREATE TABLE log (
            id         INTEGER PRIMARY KEY,
            created_at TEXT    NOT NULL DEFAULT (datetime('now', 'localtime')),
            kind       INTEGER NOT NULL CHECK (kind BETWEEN 0 AND 6),
            message    TEXT    NOT NULL
        );
        CREATE INDEX idx_log_kind ON log(kind, id);

        INSERT INTO log(created_at, kind, message)
            SELECT created_at, 5, 'Could not post ' || day || ': ' || message
            FROM issues ORDER BY id;
        DROP TABLE issues;
    )sql"
            },
            // Skip and adjust rules, and restore points.
            //  - rules.kind: 0 charge (posts items), 1 adjust (adds `extra` KES to each posting).
            //  - skips are their own table, each with targets (a whole category OR one item).
            //  - entries.skip_id: which skip turned an entry into a grey "Skipped" row.
            //  - A restore point is a snapshot of the entries in one date range, plus the on/off/
            //    deleted state of every rule and skip. The copies hold plain numbers, with no
            //    links, so deleting an item later can never block a restore.
            Migration{
                10, R"sql(
        ALTER TABLE rules ADD COLUMN kind  INTEGER NOT NULL DEFAULT 0 CHECK (kind IN (0, 1));
        ALTER TABLE rules ADD COLUMN extra INTEGER NOT NULL DEFAULT 0;

        CREATE TABLE skips (
            id         INTEGER PRIMARY KEY,
            name       TEXT    NOT NULL,
            from_date  TEXT    NOT NULL
                       CHECK (from_date GLOB '[0-9][0-9][0-9][0-9]-[0-9][0-9]-[0-9][0-9]'),
            to_date    TEXT    NOT NULL
                       CHECK (to_date GLOB '[0-9][0-9][0-9][0-9]-[0-9][0-9]-[0-9][0-9]'),
            is_deleted INTEGER NOT NULL DEFAULT 0 CHECK (is_deleted IN (0, 1)),
            CHECK (to_date >= from_date)
        );
        CREATE TABLE skip_targets (
            skip_id     INTEGER NOT NULL REFERENCES skips(id),
            category_id INTEGER REFERENCES categories(id),
            item_id     INTEGER REFERENCES items(id),
            CHECK ((category_id IS NULL) != (item_id IS NULL))   -- exactly one of the two
        );
        CREATE INDEX idx_skip_targets ON skip_targets(skip_id);

        ALTER TABLE entries ADD COLUMN skip_id INTEGER REFERENCES skips(id);

        CREATE TABLE restore_points (
            id         INTEGER PRIMARY KEY,
            created_at TEXT NOT NULL DEFAULT (datetime('now', 'localtime')),
            label      TEXT NOT NULL,
            from_date  TEXT NOT NULL,
            to_date    TEXT NOT NULL
        );
        CREATE TABLE rp_entries (
            restore_point_id INTEGER NOT NULL REFERENCES restore_points(id) ON DELETE CASCADE,
            id INTEGER NOT NULL, date TEXT NOT NULL, item_id INTEGER, category_id INTEGER,
            name TEXT NOT NULL, amount INTEGER NOT NULL, original_amount INTEGER,
            kind INTEGER NOT NULL, funded_from INTEGER NOT NULL, rule_id INTEGER, skip_id INTEGER
        );
        CREATE INDEX idx_rp_entries ON rp_entries(restore_point_id);
        CREATE TABLE rp_rules (
            restore_point_id INTEGER NOT NULL REFERENCES restore_points(id) ON DELETE CASCADE,
            rule_id INTEGER NOT NULL, is_active INTEGER NOT NULL, is_deleted INTEGER NOT NULL,
            effective_to TEXT
        );
        CREATE TABLE rp_skips (
            restore_point_id INTEGER NOT NULL REFERENCES restore_points(id) ON DELETE CASCADE,
            skip_id INTEGER NOT NULL, is_deleted INTEGER NOT NULL
        );
    )sql"
            },
            // Goals (things to buy) and funds (a target tracked against a pot).
//  - goals.funded_from uses the same codes as entries: 0 earned, 1 savings.
//  - goals.bought_on is empty until the goal is bought; bought_how is empty for a gift.
//  - The last CHECK means a goal can only buy itself if it has a date to buy on.
Migration{11, R"sql(
        CREATE TABLE goals (
            id          INTEGER PRIMARY KEY,
            name        TEXT    NOT NULL COLLATE NOCASE,
            amount      INTEGER NOT NULL CHECK (amount > 0),
            priority    INTEGER NOT NULL CHECK (priority > 0),
            due_date    TEXT    CHECK (due_date IS NULL OR
                                       due_date GLOB '[0-9][0-9][0-9][0-9]-[0-9][0-9]-[0-9][0-9]'),
            funded_from INTEGER NOT NULL DEFAULT 0 CHECK (funded_from IN (0, 1)),
            donated     INTEGER NOT NULL DEFAULT 0 CHECK (donated IN (0, 1)),
            auto_buy    INTEGER NOT NULL DEFAULT 0 CHECK (auto_buy IN (0, 1)),
            bought_on   TEXT    CHECK (bought_on IS NULL OR
                                       bought_on GLOB '[0-9][0-9][0-9][0-9]-[0-9][0-9]-[0-9][0-9]'),
            bought_how  INTEGER CHECK (bought_how IS NULL OR bought_how BETWEEN 0 AND 2),
            is_deleted  INTEGER NOT NULL DEFAULT 0 CHECK (is_deleted IN (0, 1)),
            CHECK (auto_buy = 0 OR due_date IS NOT NULL)
        );
        CREATE UNIQUE INDEX ux_goals_name ON goals(name) WHERE is_deleted = 0;

        CREATE TABLE funds (
            id         INTEGER PRIMARY KEY,
            name       TEXT    NOT NULL COLLATE NOCASE,
            target     INTEGER NOT NULL CHECK (target > 0),
            pot_id     INTEGER NOT NULL REFERENCES pots(id),
            is_deleted INTEGER NOT NULL DEFAULT 0 CHECK (is_deleted IN (0, 1))
        );
        CREATE UNIQUE INDEX ux_funds_name ON funds(name) WHERE is_deleted = 0;
    )sql"},
                // Pay history, other income sources, fund target dates, and short-lived categories and items.
    //  - pay_history: one row per amount, with the days it applied ("a raise" ends the old row the day before).
    //    A file from before this migration brings its single expected pay across as the first row.
    //  - income_sources.frequency: 0 one-off, 1 with my pay, 2 monthly, 3 every 3 months, 4 yearly.
    //    "With my pay" income has no date of its own (the last CHECK).
    //  - expires_on: the LAST day a category or item is in use; it pauses itself after that.
    Migration{12, R"sql(
        CREATE TABLE pay_history (
            id        INTEGER PRIMARY KEY,
            amount    INTEGER NOT NULL CHECK (amount > 0),
            from_date TEXT    NOT NULL CHECK (from_date GLOB '[0-9][0-9][0-9][0-9]-[0-9][0-9]-[0-9][0-9]'),
            to_date   TEXT    CHECK (to_date IS NULL OR
                                     to_date GLOB '[0-9][0-9][0-9][0-9]-[0-9][0-9]-[0-9][0-9]'),
            CHECK (to_date IS NULL OR to_date >= from_date)
        );
        CREATE UNIQUE INDEX ux_pay_history_from ON pay_history(from_date);
        INSERT INTO pay_history(amount, from_date)
            SELECT CAST(value AS INTEGER),
                   COALESCE((SELECT MIN(start_date) FROM periods), date('now', 'localtime'))
            FROM settings WHERE key = 'expected_pay' AND CAST(value AS INTEGER) > 0;

        CREATE TABLE income_sources (
            id            INTEGER PRIMARY KEY,
            name          TEXT    NOT NULL COLLATE NOCASE,
            usual_amount  INTEGER NOT NULL CHECK (usual_amount > 0),
            frequency     INTEGER NOT NULL CHECK (frequency BETWEEN 0 AND 4),
            next_expected TEXT    CHECK (next_expected IS NULL OR
                                         next_expected GLOB '[0-9][0-9][0-9][0-9]-[0-9][0-9]-[0-9][0-9]'),
            pot_id        INTEGER REFERENCES pots(id),      -- NULL = earned
            snoozed_on    TEXT    CHECK (snoozed_on IS NULL OR
                                         snoozed_on GLOB '[0-9][0-9][0-9][0-9]-[0-9][0-9]-[0-9][0-9]'),
            is_deleted    INTEGER NOT NULL DEFAULT 0 CHECK (is_deleted IN (0, 1)),
            CHECK (frequency != 1 OR next_expected IS NULL)
        );
        CREATE UNIQUE INDEX ux_income_sources_name ON income_sources(name) WHERE is_deleted = 0;

        ALTER TABLE funds      ADD COLUMN target_date TEXT
            CHECK (target_date IS NULL OR target_date GLOB '[0-9][0-9][0-9][0-9]-[0-9][0-9]-[0-9][0-9]');
        ALTER TABLE categories ADD COLUMN expires_on TEXT
            CHECK (expires_on IS NULL OR expires_on GLOB '[0-9][0-9][0-9][0-9]-[0-9][0-9]-[0-9][0-9]');
        ALTER TABLE items      ADD COLUMN expires_on TEXT
            CHECK (expires_on IS NULL OR expires_on GLOB '[0-9][0-9][0-9][0-9]-[0-9][0-9]-[0-9][0-9]');
    )sql"},
            // Estimated goals: the amount can be a guess, and buying records the real price.
Migration{13, R"sql(
        ALTER TABLE goals ADD COLUMN is_estimate INTEGER NOT NULL DEFAULT 0 CHECK (is_estimate IN (0, 1));
        ALTER TABLE goals ADD COLUMN paid_amount INTEGER CHECK (paid_amount IS NULL OR paid_amount > 0);
    )sql"},
        };

        // While tables are being rebuilt, foreign keys must be switched off (SQLite ignores
        // that switch inside a transaction, so this has to wrap the Transaction). The
        // destructor switches them back on, even if the migration throws.
        class ForeignKeysOff {
        public:
            explicit ForeignKeysOff(Database &db) : db_(db) { db_.exec("PRAGMA foreign_keys = OFF;"); }

            ~ForeignKeysOff() {
                try { db_.exec("PRAGMA foreign_keys = ON;"); } catch (...) {
                }
            }

            ForeignKeysOff(const ForeignKeysOff &) = delete;

            ForeignKeysOff &operator=(const ForeignKeysOff &) = delete;

        private:
            Database &db_;
        };

        // Because checking was off, verify nothing now points at a missing row.
        void requireForeignKeysIntact(Database &db, int version) {
            auto st = db.prepare("PRAGMA foreign_key_check");
            if (st.step()) {
                throw Error("Migration " + std::to_string(version) + " would break the data. Nothing was changed.");
            }
        }
    } // namespace

    int latestSchemaVersion() { return kMigrations.back().version; }

    int currentSchemaVersion(Database &db) {
        db.exec("CREATE TABLE IF NOT EXISTS meta (key TEXT PRIMARY KEY, value TEXT NOT NULL);");
        auto st = db.prepare("SELECT value FROM meta WHERE key = 'schema_version'");
        return st.step() ? std::stoi(st.text(0)) : 0; // no row = brand new file
    }

    void migrate(Database &db) {
        const int current = currentSchemaVersion(db);

        if (current > latestSchemaVersion()) {
            throw Error("This database was made by a newer Pesa. Please update Pesa.");
        }

        for (const Migration &m: kMigrations) {
            if (m.version <= current) continue;

            ForeignKeysOff fkOff(db); // declared first, so it is undone last
            Transaction tx(db); // a failed step leaves the file untouched
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
