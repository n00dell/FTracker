// FTracker/tests/test_migrations.cpp
#include <doctest/doctest.h>

#include <pesa/pesa.h>

#include "raw_sql.h"
#include "temp_db.h"

using pesa::Error;
using pesa::Pesa;
using pesa::Visibility;

namespace {
    // Bump this when you add a migration. It is the only place the number lives in the tests.
    constexpr int kLatest = 11;

    // Frozen copies of what older Pesa versions created. NEVER change these:
    // they are the "old files" the upgrade code has to cope with.
    constexpr const char *kV1 = R"sql(
    CREATE TABLE meta (key TEXT PRIMARY KEY, value TEXT NOT NULL);
    CREATE TABLE categories (
        id INTEGER PRIMARY KEY, name TEXT NOT NULL UNIQUE COLLATE NOCASE,
        sort_order INTEGER NOT NULL, is_active INTEGER NOT NULL DEFAULT 1);
    INSERT INTO categories(name, sort_order) VALUES ('Food', 1);
    INSERT INTO meta VALUES ('schema_version', '1');
)sql";

    // Same, but the person already made a category called Misc by hand.
    constexpr const char *kV1WithMisc = R"sql(
    CREATE TABLE meta (key TEXT PRIMARY KEY, value TEXT NOT NULL);
    CREATE TABLE categories (
        id INTEGER PRIMARY KEY, name TEXT NOT NULL UNIQUE COLLATE NOCASE,
        sort_order INTEGER NOT NULL, is_active INTEGER NOT NULL DEFAULT 1);
    INSERT INTO categories(name, sort_order) VALUES ('Misc', 1);
    INSERT INTO meta VALUES ('schema_version', '1');
)sql";

    constexpr const char *kV2 = R"sql(
    CREATE TABLE meta (key TEXT PRIMARY KEY, value TEXT NOT NULL);
    CREATE TABLE categories (
        id INTEGER PRIMARY KEY, name TEXT NOT NULL UNIQUE COLLATE NOCASE,
        sort_order INTEGER NOT NULL, is_active INTEGER NOT NULL DEFAULT 1);
    CREATE TABLE items (
        id INTEGER PRIMARY KEY, category_id INTEGER NOT NULL REFERENCES categories(id),
        name TEXT NOT NULL COLLATE NOCASE, default_amount INTEGER NOT NULL CHECK (default_amount > 0),
        weekdays INTEGER NOT NULL DEFAULT 127, is_active INTEGER NOT NULL DEFAULT 1,
        UNIQUE (category_id, name));
    INSERT INTO categories(name, sort_order, is_active) VALUES ('Food', 1, 1), ('Transport', 2, 0);
    INSERT INTO items(category_id, name, default_amount, weekdays, is_active)
        VALUES (1, 'Milk', 60, 31, 1), (1, 'Bread', 55, 127, 0), (2, 'Matatu', 50, 96, 1);
    INSERT INTO meta VALUES ('schema_version', '2');
)sql";
} // namespace

TEST_CASE(
    "a new database is created at the latest version"
) {
    Pesa app(":memory:");
    CHECK(app.schemaVersion() == kLatest);
}

TEST_CASE(
    "a version 1 file is upgraded all the way and keeps its data"
) {
    TempDb file("pesa_test_v1_upgrade.db");
    rawSql(file.path, kV1);

    Pesa app(file.path);
    CHECK(app.schemaVersion() == kLatest);
    const auto cats = app.categories().list();
    REQUIRE(cats.size() == 2); // Food, plus the new built-in Misc
    CHECK(cats[0].name == "Food");
    CHECK(cats[1].isSystem);
    CHECK_NOTHROW(app.items().add({.categoryId = cats[0].id, .name = "Milk", .defaultAmount = 60}));
}

TEST_CASE(
    "a Misc category you made yourself is adopted, not duplicated"
) {
    TempDb file("pesa_test_v1_misc.db");
    rawSql(file.path, kV1WithMisc);

    Pesa app(file.path);
    const auto cats = app.categories().list();
    REQUIRE(cats.size() == 1);
    CHECK(cats[0].name == "Misc");
    CHECK(cats[0].isSystem);
}

TEST_CASE(
    "a version 2 file survives the table rebuild of migration 3"
) {
    TempDb file("pesa_test_v2_upgrade.db");
    rawSql(file.path, kV2);

    {
        Pesa app(file.path);
        CHECK(app.schemaVersion() == kLatest);

        // Categories: both kept, with their on/off state and order. Misc is new and last.
        const auto cats = app.categories().list(Visibility::IncludeInactive);
        REQUIRE(cats.size() == 3);
        CHECK(cats[0].name == "Food");
        CHECK(cats[0].isActive);
        CHECK(cats[1].name == "Transport");
        CHECK_FALSE(cats[1].isActive);
        CHECK(cats[2].isSystem);
        CHECK_FALSE(cats[0].isDeleted);

        // Items: ids, amounts and active flags all kept.
        const auto items = app.items().list(std::nullopt, Visibility::IncludeInactive);
        REQUIRE(items.size() == 3);
        CHECK(items[0].name == "Milk");
        CHECK(items[0].defaultAmount == 60);
        CHECK(items[1].name == "Bread");
        CHECK_FALSE(items[1].isActive);
        CHECK(items[2].categoryName == "Transport");

        // The name rules still work after the rebuild, and soft delete frees a name.
        CHECK_THROWS_AS(app.items().add({.categoryId = cats[0].id, .name = "milk", .defaultAmount = 1}), Error);
        app.items().remove(items[0].id);
        CHECK_NOTHROW(app.items().add({.categoryId = cats[0].id, .name = "Milk", .defaultAmount = 60}));
    }

    // The link from items to categories must still exist after the rename dance.
    CHECK(firstValue(file.path, "SELECT \"table\" FROM pragma_foreign_key_list('items')") == "categories");
}

TEST_CASE(
    "a file from a newer Pesa is refused"
) {
    TempDb file("pesa_test_newer.db");
    rawSql(file.path, R"sql(
        CREATE TABLE meta (key TEXT PRIMARY KEY, value TEXT NOT NULL);
        INSERT INTO meta VALUES ('schema_version', '999');
    )sql");
    CHECK_THROWS_AS(Pesa{file.path}, Error);
}

TEST_CASE(
    "the old issues table is folded into the log"
) {
    TempDb file("pesa_test_log_table.db");
    {
        Pesa app(file.path);
    }
    CHECK(firstValue(file.path, "SELECT COUNT(*) FROM sqlite_master WHERE name = 'issues'") == "0");
    CHECK(firstValue(file.path, "SELECT COUNT(*) FROM sqlite_master WHERE name = 'log'") == "1");
}
