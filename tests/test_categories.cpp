#include <doctest/doctest.h>

#include <pesa/pesa.h>

#include "temp_db.h"

using pesa::Error;
using pesa::Pesa;
using pesa::Visibility;

// ":memory:" = a throwaway database that lives only inside the test.
TEST_CASE("a new database has no categories") {
    Pesa app(":memory:");
    CHECK(app.categories().list().empty());
}

TEST_CASE("categories are listed in the order they were added") {
    Pesa app(":memory:");
    app.categories().add("Food");
    app.categories().add("Transport");
    const auto list = app.categories().list();
    REQUIRE(list.size() == 2);
    CHECK(list[0].name == "Food");
    CHECK(list[1].name == "Transport");
    CHECK(list[0].sortOrder < list[1].sortOrder);
}

TEST_CASE("names are trimmed, and blank or duplicate names are refused") {
    Pesa app(":memory:");
    app.categories().add("  Food  ");
    CHECK(app.categories().list()[0].name == "Food");
    CHECK_THROWS_AS(app.categories().add("   "), Error);
    CHECK_THROWS_AS(app.categories().add("food"), Error);   // case-insensitive
}

TEST_CASE("renaming keeps the id") {
    Pesa app(":memory:");
    const auto id = app.categories().add("Fod");
    app.categories().add("Home");
    app.categories().rename(id, "Food");
    CHECK(app.categories().list()[0].id == id);
    CHECK(app.categories().list()[0].name == "Food");
    CHECK_THROWS_AS(app.categories().rename(id, "home"), Error);
    CHECK_THROWS_AS(app.categories().rename(9999, "X"), Error);
    CHECK_NOTHROW(app.categories().rename(id, "FOOD"));   // changing only the case is allowed
}

TEST_CASE("deactivating pauses a category but keeps its name reserved") {
    Pesa app(":memory:");
    const auto id = app.categories().add("Food");
    app.categories().add("Home");

    app.categories().setActive(id, false);
    CHECK(app.categories().list().size() == 1);
    REQUIRE(app.categories().list(Visibility::IncludeInactive).size() == 2);
    CHECK_FALSE(app.categories().list(Visibility::IncludeInactive)[0].isActive);
    CHECK_THROWS_AS(app.categories().add("Food"), Error);   // name still reserved

    app.categories().setActive(id, true);
    CHECK(app.categories().list().size() == 2);
    CHECK_THROWS_AS(app.categories().setActive(9999, false), Error);
}

TEST_CASE("deleting hides a category, keeps the row, and frees the name") {
    Pesa app(":memory:");
    const auto old = app.categories().add("Food");
    app.categories().remove(old);

    CHECK(app.categories().list().empty());
    CHECK(app.categories().list(Visibility::IncludeInactive).empty());   // deleted is not "inactive"
    REQUIRE(app.categories().list(Visibility::DeletedOnly).size() == 1);
    CHECK(app.categories().find(old)->isDeleted);                        // history can still find it

    const auto fresh = app.categories().add("Food");                     // same name again
    CHECK(fresh != old);
    CHECK(app.categories().list().size() == 1);
}

TEST_CASE("a deleted category cannot be edited, only restored") {
    Pesa app(":memory:");
    const auto id = app.categories().add("Food");
    app.categories().remove(id);
    CHECK_THROWS_AS(app.categories().rename(id, "Other"), Error);
    CHECK_THROWS_AS(app.categories().setActive(id, false), Error);
    CHECK_THROWS_AS(app.categories().remove(id), Error);                 // already deleted
    CHECK_THROWS_AS(app.categories().restore(9999), Error);
}

TEST_CASE("restoring a category") {
    Pesa app(":memory:");
    const auto id = app.categories().add("Food");
    CHECK_THROWS_AS(app.categories().restore(id), Error);                // not deleted

    app.categories().remove(id);
    app.categories().restore(id);
    CHECK(app.categories().list().size() == 1);
    CHECK_FALSE(app.categories().find(id)->isDeleted);

    app.categories().remove(id);
    app.categories().add("Food");                                         // name reused
    CHECK_THROWS_AS(app.categories().restore(id), Error);                // would clash
}

TEST_CASE("data survives closing and reopening the file") {
    TempDb file("pesa_test_reopen.db");
    {
        Pesa app(file.path);
        app.categories().add("Food");
    }
    {
        Pesa app(file.path);   // migrations must not run twice or wipe anything
        REQUIRE(app.categories().list().size() == 1);
        CHECK(app.categories().list()[0].name == "Food");
    }
}
