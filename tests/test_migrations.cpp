#include <doctest/doctest.h>

#include <pesa/pesa.h>
#include <sqlite3.h>   // available because pesa_core links SQLite publicly

#include "temp_db.h"

using pesa::Error;
using pesa::Pesa;
using pesa::Visibility;

namespace {

// Runs raw SQL on a file. Used to fake "a database an older Pesa created".
void rawSql(const std::filesystem::path& file, const char* sql) {
    sqlite3* db = nullptr;
    REQUIRE(sqlite3_open(file.string().c_str(), &db) == SQLITE_OK);
    char* err = nullptr;
    const int rc = sqlite3_exec(db, sql, nullptr, nullptr, &err);
    sqlite3_free(err);
    sqlite3_close(db);
    REQUIRE(rc == SQLITE_OK);
}

// Runs a query and returns the first column of the first row as text.
std::string firstValue(const std::filesystem::path& file, const char* sql) {
    sqlite3* db = nullptr;
    REQUIRE(sqlite3_open(file.string().c_str(), &db) == SQLITE_OK);
    std::string result;
    auto onRow = [](void* out, int columns, char** values, char**) -> int {
        if (columns > 0 && values[0]) *static_cast<std::string*>(out) = values[0];
        return 0;
    };
    const int rc = sqlite3_exec(db, sql, onRow, &result, nullptr);
    sqlite3_close(db);
    REQUIRE(rc == SQLITE_OK);
    return result;
}

// Frozen copies of what older Pesa versions created. NEVER change these:
// they are the "old files" the upgrade code has to cope with.
constexpr const char* kV1 = R"sql(
    CREATE TABLE meta (key TEXT PRIMARY KEY, value TEXT NOT NULL);
    CREATE TABLE categories (
        id INTEGER PRIMARY KEY, name TEXT NOT NULL UNIQUE COLLATE NOCASE,
        sort_order INTEGER NOT NULL, is_active INTEGER NOT NULL DEFAULT 1);
    INSERT INTO categories(name, sort_order) VALUES ('Food', 1);
    INSERT INTO meta VALUES ('schema_version', '1');
)sql";

constexpr const char* kV2 = R"sql(
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

TEST_CASE("a new database is created at the latest version") {
    Pesa app(":memory:");
    CHECK(app.schemaVersion() == 3);
}

TEST_CASE("a version 1 file is upgraded all the way and keeps its data") {
    TempDb file("pesa_test_v1_upgrade.db");
    rawSql(file.path, kV1);

    Pesa app(file.path);
    CHECK(app.schemaVersion() == 3);
    REQUIRE(app.categories().list().size() == 1);   // old data is still there
    const auto cat = app.categories().list()[0].id;
    CHECK_NOTHROW(app.items().add({.categoryId = cat, .name = "Milk", .defaultAmount = 60}));
}

TEST_CASE("a version 2 file survives the table rebuild of migration 3") {
    TempDb file("pesa_test_v2_upgrade.db");
    rawSql(file.path, kV2);

    {
        Pesa app(file.path);
        CHECK(app.schemaVersion() == 3);

        // Categories: both kept, with their on/off state and order.
        const auto cats = app.categories().list(Visibility::IncludeInactive);
        REQUIRE(cats.size() == 2);
        CHECK(cats[0].name == "Food");
        CHECK(cats[0].isActive);
        CHECK(cats[1].name == "Transport");
        CHECK_FALSE(cats[1].isActive);
        CHECK_FALSE(cats[0].isDeleted);

        // Items: ids, amounts, days and active flags all kept.
        const auto items = app.items().list(std::nullopt, Visibility::IncludeInactive);
        REQUIRE(items.size() == 3);
        CHECK(items[0].name == "Milk");
        CHECK(items[0].defaultAmount == 60);
        CHECK(items[0].days == pesa::WeekdaySet::workDays());
        CHECK(items[1].name == "Bread");
        CHECK_FALSE(items[1].isActive);
        CHECK(items[2].categoryName == "Transport");
        CHECK(items[2].days == pesa::WeekdaySet::weekend());

        // The name rules still work after the rebuild, and soft delete frees a name.
        CHECK_THROWS_AS(app.items().add({.categoryId = cats[0].id, .name = "milk", .defaultAmount = 1}), Error);
        app.items().remove(items[0].id);
        CHECK_NOTHROW(app.items().add({.categoryId = cats[0].id, .name = "Milk", .defaultAmount = 60}));
    }

    // The link from items to categories must still exist after the rename dance.
    CHECK(firstValue(file.path, "SELECT \"table\" FROM pragma_foreign_key_list('items')") == "categories");
}

TEST_CASE("a file from a newer Pesa is refused") {
    TempDb file("pesa_test_newer.db");
    rawSql(file.path, R"sql(
        CREATE TABLE meta (key TEXT PRIMARY KEY, value TEXT NOT NULL);
        INSERT INTO meta VALUES ('schema_version', '999');
    )sql");
    CHECK_THROWS_AS(Pesa{file.path}, Error);
}
