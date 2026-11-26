// FTracker/tests/test_items.cpp
#include <doctest/doctest.h>

#include <pesa/pesa.h>

using pesa::Error;
using pesa::Pesa;
using pesa::Visibility;
using pesa::Weekday;
using pesa::WeekdaySet;

TEST_CASE (

"adding an item stores everything"
)
 {
    Pesa app(":memory:");
    const auto cat = app.categories().add("Transport");
    const auto id = app.items().add({.categoryId = cat, .name = "  Matatu to town ", .defaultAmount = 50});

    const auto item = app.items().find(id);
    REQUIRE(item.has_value());
    CHECK(item->name == "Matatu to town");
    CHECK(item->categoryName == "Transport");
    CHECK(item->categoryId == cat);
    CHECK(item->defaultAmount == 50);
    CHECK(item->isActive);
    CHECK_FALSE(item->isDeleted);
}

TEST_CASE (

"bad input is refused"
)
 {
    Pesa app(":memory:");
    const auto cat = app.categories().add("Food");
    CHECK_THROWS_AS(app.items().add({.categoryId = cat, .name = "  ", .defaultAmount = 10}), Error);
    CHECK_THROWS_AS(app.items().add({.categoryId = cat, .name = "Milk", .defaultAmount = 0}), Error);
    CHECK_THROWS_AS(app.items().add({.categoryId = cat, .name = "Milk", .defaultAmount = -5}), Error);
    CHECK_THROWS_AS(app.items().add({.categoryId = 999, .name = "Milk", .defaultAmount = 10}), Error);
    CHECK(app.items().list().empty());   // nothing half-saved
}

TEST_CASE (

"names are unique inside a category, not across categories"
)
 {
    Pesa app(":memory:");
    const auto food = app.categories().add("Food");
    const auto home = app.categories().add("Home");
    app.items().add({.categoryId = food, .name = "Other", .defaultAmount = 10});

    CHECK_THROWS_AS(app.items().add({.categoryId = food, .name = "other", .defaultAmount = 20}), Error);
    CHECK_NOTHROW(app.items().add({.categoryId = home, .name = "Other", .defaultAmount = 20}));
}

TEST_CASE (

"an item cannot be added to an inactive or deleted category"
)
 {
    Pesa app(":memory:");
    const auto cat = app.categories().add("Food");
    app.categories().setActive(cat, false);
    CHECK_THROWS_AS(app.items().add({.categoryId = cat, .name = "Milk", .defaultAmount = 10}), Error);

    app.categories().setActive(cat, true);
    app.categories().remove(cat);
    CHECK_THROWS_AS(app.items().add({.categoryId = cat, .name = "Milk", .defaultAmount = 10}), Error);
}

TEST_CASE (

"rename and change amount"
)
 {
    Pesa app(":memory:");
    const auto cat = app.categories().add("Food");
    const auto milk = app.items().add({.categoryId = cat, .name = "Milk", .defaultAmount = 60});
    app.items().add({.categoryId = cat, .name = "Bread", .defaultAmount = 55});

    app.items().rename(milk, "Fresh milk");
    app.items().setDefaultAmount(milk, 65);
    CHECK(app.items().find(milk)->name == "Fresh milk");
    CHECK(app.items().find(milk)->defaultAmount == 65);

    CHECK_THROWS_AS(app.items().rename(milk, "bread"), Error);
    CHECK_THROWS_AS(app.items().setDefaultAmount(milk, 0), Error);
    CHECK_NOTHROW(app.items().rename(milk, "FRESH MILK"));   // only the case changes
}

TEST_CASE (

"unknown ids are reported"
)
 {
    Pesa app(":memory:");
    CHECK_FALSE(app.items().find(42).has_value());
    CHECK_THROWS_AS(app.items().rename(42, "X"), Error);
    CHECK_THROWS_AS(app.items().setDefaultAmount(42, 10), Error);
    CHECK_THROWS_AS(app.items().setActive(42, false), Error);
    CHECK_THROWS_AS(app.items().remove(42), Error);
    CHECK_THROWS_AS(app.items().restore(42), Error);
}

TEST_CASE (

"listing: order, category filter, and inactive items"
)
 {
    Pesa app(":memory:");
    const auto food = app.categories().add("Food");
    const auto transport = app.categories().add("Transport");
    const auto bread = app.items().add({.categoryId = food, .name = "Bread", .defaultAmount = 55});
    app.items().add({.categoryId = transport, .name = "Matatu", .defaultAmount = 50});
    app.items().add({.categoryId = food, .name = "Milk", .defaultAmount = 60});

    const auto all = app.items().list();
    REQUIRE(all.size() == 3);
    CHECK(all[0].name == "Bread");     // category order first (Food), then insertion order
    CHECK(all[1].name == "Milk");
    CHECK(all[2].name == "Matatu");
    CHECK(app.items().list(transport).size() == 1);

    app.items().setActive(bread, false);
    CHECK(app.items().list().size() == 2);
    CHECK(app.items().list(std::nullopt, Visibility::IncludeInactive).size() == 3);
}

TEST_CASE (

"an inactive item keeps its name reserved"
)
 {
    Pesa app(":memory:");
    const auto cat = app.categories().add("Food");
    const auto milk = app.items().add({.categoryId = cat, .name = "Milk", .defaultAmount = 60});
    app.items().setActive(milk, false);
    CHECK_THROWS_AS(app.items().add({.categoryId = cat, .name = "Milk", .defaultAmount = 60}), Error);
}

TEST_CASE (

"deleting an item frees its name so the same item can be created again"
)
 {
    Pesa app(":memory:");
    const auto cat = app.categories().add("Food");
    const auto old = app.items().add({.categoryId = cat, .name = "Milk", .defaultAmount = 60});
    app.items().remove(old);

    CHECK(app.items().list().empty());
    REQUIRE(app.items().list(std::nullopt, Visibility::DeletedOnly).size() == 1);
    CHECK(app.items().find(old)->isDeleted);                  // the old row is still there

    const auto fresh = app.items().add({.categoryId = cat, .name = "Milk", .defaultAmount = 60});
    CHECK(fresh != old);                                      // a brand new row
    CHECK(app.items().list().size() == 1);
}

TEST_CASE (

"a deleted item cannot be edited"
)
 {
    Pesa app(":memory:");
    const auto cat = app.categories().add("Food");
    const auto milk = app.items().add({.categoryId = cat, .name = "Milk", .defaultAmount = 60});
    app.items().remove(milk);
    CHECK_THROWS_AS(app.items().rename(milk, "X"), Error);
    CHECK_THROWS_AS(app.items().setDefaultAmount(milk, 70), Error);
    CHECK_THROWS_AS(app.items().setActive(milk, false), Error);
    CHECK_THROWS_AS(app.items().remove(milk), Error);
}

TEST_CASE (

"restoring an item"
)
 {
    Pesa app(":memory:");
    const auto cat = app.categories().add("Food");
    const auto milk = app.items().add({.categoryId = cat, .name = "Milk", .defaultAmount = 60});
    CHECK_THROWS_AS(app.items().restore(milk), Error);        // not deleted

    app.items().remove(milk);
    app.items().restore(milk);
    CHECK(app.items().list().size() == 1);
    CHECK(app.items().find(milk)->defaultAmount == 60);       // nothing was lost

    app.items().remove(milk);
    app.items().add({.categoryId = cat, .name = "Milk", .defaultAmount = 70});   // name reused
    CHECK_THROWS_AS(app.items().restore(milk), Error);        // would clash
}

TEST_CASE (

"deactivating a category hides its items without changing them"
)
 {
    Pesa app(":memory:");
    const auto food = app.categories().add("Food");
    const auto milk = app.items().add({.categoryId = food, .name = "Milk", .defaultAmount = 60});

    app.categories().setActive(food, false);
    CHECK(app.items().list().empty());
    CHECK(app.items().list(std::nullopt, Visibility::IncludeInactive).size() == 1);
    CHECK(app.items().find(milk)->isActive);   // the item itself was never touched

    app.categories().setActive(food, true);
    CHECK(app.items().list().size() == 1);
}

TEST_CASE (

"deleting a category hides its items, and restoring it brings them back"
)
 {
    Pesa app(":memory:");
    const auto food = app.categories().add("Food");
    const auto milk = app.items().add({.categoryId = food, .name = "Milk", .defaultAmount = 60});

    app.categories().remove(food);
    CHECK(app.items().list().empty());
    CHECK(app.items().list(std::nullopt, Visibility::IncludeInactive).empty());
    CHECK(app.items().list(std::nullopt, Visibility::DeletedOnly).empty());   // the item itself is not deleted

    app.categories().restore(food);
    CHECK(app.items().list().size() == 1);                                    // back with its category
}

TEST_CASE (

"an item cannot be restored while its category is deleted"
)
 {
    Pesa app(":memory:");
    const auto food = app.categories().add("Food");
    const auto milk = app.items().add({.categoryId = food, .name = "Milk", .defaultAmount = 60});
    app.items().remove(milk);
    app.categories().remove(food);

    CHECK_THROWS_AS(app.items().restore(milk), Error);
    app.categories().restore(food);
    CHECK_NOTHROW(app.items().restore(milk));
    CHECK(app.items().list().size() == 1);
}
TEST_CASE (

"an item can live in the built-in Misc category"
)
 {
    Pesa app(":memory:");
    const auto id = app.items().add({.categoryId = app.categories().misc(), .name = "Bank fee", .defaultAmount = 30});
    CHECK(app.items().find(id)->categoryName == "Misc");
}