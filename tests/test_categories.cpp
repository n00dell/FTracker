// FTracker/tests/test_categories.cpp
#include <doctest/doctest.h>

#include <pesa/pesa.h>

#include "temp_db.h"

using pesa::Error;
using pesa::Pesa;
using pesa::Visibility;

namespace {

// Misc is built in and always there, so most tests look only at categories the user made.
std::vector<pesa::Category> mine(const Pesa& app, Visibility show = Visibility::ActiveOnly) {
    std::vector<pesa::Category> out;
    for (auto& c : app.categories().list(show)) {
        if (!c.isSystem) out.push_back(std::move(c));
    }
    return out;
}

} // namespace

// ":memory:" = a throwaway database that lives only inside the test.
TEST_CASE("a new database has only the built-in Misc category") {
    Pesa app(":memory:");
    const auto all = app.categories().list();
    REQUIRE(all.size() == 1);
    CHECK(all[0].name == "Misc");
    CHECK(all[0].isSystem);
    CHECK(all[0].id == app.categories().misc());
    CHECK(mine(app).empty());
}

TEST_CASE("Misc cannot be renamed, paused or deleted, and its name is taken") {
    Pesa app(":memory:");
    const auto misc = app.categories().misc();
    CHECK_THROWS_AS(app.categories().rename(misc, "Other"), Error);
    CHECK_THROWS_AS(app.categories().setActive(misc, false), Error);
    CHECK_THROWS_AS(app.categories().remove(misc), Error);
    CHECK_THROWS_AS(app.categories().add("misc"), Error);
}

TEST_CASE("categories are listed in the order they were added, with Misc last") {
    Pesa app(":memory:");
    app.categories().add("Food");
    app.categories().add("Transport");
    const auto list = app.categories().list();
    REQUIRE(list.size() == 3);
    CHECK(list[0].name == "Food");
    CHECK(list[1].name == "Transport");
    CHECK(list[2].name == "Misc");
}

TEST_CASE("names are trimmed, and blank or duplicate names are refused") {
    Pesa app(":memory:");
    app.categories().add("  Food  ");
    CHECK(mine(app)[0].name == "Food");
    CHECK_THROWS_AS(app.categories().add("   "), Error);
    CHECK_THROWS_AS(app.categories().add("food"), Error);   // case-insensitive
}

TEST_CASE("renaming keeps the id") {
    Pesa app(":memory:");
    const auto id = app.categories().add("Fod");
    app.categories().add("Home");
    app.categories().rename(id, "Food");
    CHECK(mine(app)[0].id == id);
    CHECK(mine(app)[0].name == "Food");
    CHECK_THROWS_AS(app.categories().rename(id, "home"), Error);
    CHECK_THROWS_AS(app.categories().rename(9999, "X"), Error);
    CHECK_NOTHROW(app.categories().rename(id, "FOOD"));   // changing only the case is allowed
}

TEST_CASE("deactivating pauses a category but keeps its name reserved") {
    Pesa app(":memory:");
    const auto id = app.categories().add("Food");
    app.categories().add("Home");

    app.categories().setActive(id, false);
    CHECK(mine(app).size() == 1);
    REQUIRE(mine(app, Visibility::IncludeInactive).size() == 2);
    CHECK_FALSE(mine(app, Visibility::IncludeInactive)[0].isActive);
    CHECK_THROWS_AS(app.categories().add("Food"), Error);   // name still reserved

    app.categories().setActive(id, true);
    CHECK(mine(app).size() == 2);
    CHECK_THROWS_AS(app.categories().setActive(9999, false), Error);
}

TEST_CASE("deleting hides a category, keeps the row, and frees the name") {
    Pesa app(":memory:");
    const auto old = app.categories().add("Food");
    app.categories().remove(old);

    CHECK(mine(app).empty());
    CHECK(mine(app, Visibility::IncludeInactive).empty());   // deleted is not "inactive"
    REQUIRE(app.categories().list(Visibility::DeletedOnly).size() == 1);
    CHECK(app.categories().find(old)->isDeleted);            // history can still find it

    const auto fresh = app.categories().add("Food");         // same name again
    CHECK(fresh != old);
    CHECK(mine(app).size() == 1);
}

TEST_CASE("a deleted category cannot be edited, only restored") {
    Pesa app(":memory:");
    const auto id = app.categories().add("Food");
    app.categories().remove(id);
    CHECK_THROWS_AS(app.categories().rename(id, "Other"), Error);
    CHECK_THROWS_AS(app.categories().setActive(id, false), Error);
    CHECK_THROWS_AS(app.categories().remove(id), Error);     // already deleted
    CHECK_THROWS_AS(app.categories().restore(9999), Error);
}

TEST_CASE("restoring a category") {
    Pesa app(":memory:");
    const auto id = app.categories().add("Food");
    CHECK_THROWS_AS(app.categories().restore(id), Error);    // not deleted

    app.categories().remove(id);
    app.categories().restore(id);
    CHECK(mine(app).size() == 1);
    CHECK_FALSE(app.categories().find(id)->isDeleted);

    app.categories().remove(id);
    app.categories().add("Food");                            // name reused
    CHECK_THROWS_AS(app.categories().restore(id), Error);    // would clash
}

TEST_CASE("data survives closing and reopening the file") {
    TempDb file("pesa_test_reopen.db");
    {
        Pesa app(file.path);
        app.categories().add("Food");
    }
    {
        Pesa app(file.path);   // migrations must not run twice or wipe anything
        REQUIRE(mine(app).size() == 1);
        CHECK(mine(app)[0].name == "Food");
        CHECK(app.categories().list().size() == 2);   // and Misc was not created twice
    }
}