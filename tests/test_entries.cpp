//
// Created by lee on 11/23/26.
//
#include <doctest/doctest.h>

#include <pesa/pesa.h>

#include "raw_sql.h"
#include "temp_db.h"

using pesa::Date;
using pesa::EntryId;
using pesa::EntryKind;
using pesa::Error;
using pesa::FundedFrom;
using pesa::Pesa;

namespace {

const Date kDay = Date::parse("2026-11-23");

// A tiny starting point: Transport with Matatu (50), and Food with Milk (60).
struct Fixture {
    Pesa app;
    pesa::CategoryId transport = 0;
    pesa::CategoryId food = 0;
    pesa::ItemId matatu = 0;
    pesa::ItemId milk = 0;

    explicit Fixture(const std::filesystem::path& file = ":memory:") : app(file) {
        food = app.categories().add("Food");            // Food first, Transport second
        transport = app.categories().add("Transport");
        matatu = app.items().add({.categoryId = transport, .name = "Matatu", .defaultAmount = 50});
        milk = app.items().add({.categoryId = food, .name = "Milk", .defaultAmount = 60});
    }
};

} // namespace

TEST_CASE("adding from the catalog copies the item's name, category and default amount") {
    Fixture f;
    const auto id = f.app.entries().addFromItem(kDay, f.matatu);
    const auto e = f.app.entries().find(id);
    REQUIRE(e.has_value());
    CHECK(e->date == kDay);
    CHECK(e->itemId == f.matatu);
    CHECK(e->categoryId == f.transport);
    CHECK(e->categoryName == "Transport");
    CHECK(e->name == "Matatu");
    CHECK(e->amount == 50);
    CHECK_FALSE(e->originalAmount.has_value());
    CHECK(e->kind == EntryKind::Added);
    CHECK(e->fund == FundedFrom::Earned);
    CHECK_FALSE(e->isOneOff());
}

TEST_CASE("the amount and the funding source can be chosen when adding") {
    Fixture f;
    const auto id = f.app.entries().addFromItem(kDay, f.matatu, 120, FundedFrom::Savings);
    CHECK(f.app.entries().find(id)->amount == 120);
    CHECK(f.app.entries().find(id)->fund == FundedFrom::Savings);
}

TEST_CASE("only available items can be picked, and amounts must be positive") {
    Fixture f;
    CHECK_THROWS_AS(f.app.entries().addFromItem(kDay, 9999), Error);
    CHECK_THROWS_AS(f.app.entries().addFromItem(kDay, f.matatu, 0), Error);
    CHECK_THROWS_AS(f.app.entries().addFromItem(kDay, f.matatu, -5), Error);

    f.app.items().setActive(f.matatu, false);                    // paused
    CHECK_THROWS_AS(f.app.entries().addFromItem(kDay, f.matatu), Error);
    f.app.items().setActive(f.matatu, true);
    CHECK_NOTHROW(f.app.entries().addFromItem(kDay, f.matatu));

    f.app.items().remove(f.milk);                                // deleted
    CHECK_THROWS_AS(f.app.entries().addFromItem(kDay, f.milk), Error);

    f.app.categories().setActive(f.transport, false);            // its category is paused
    CHECK_THROWS_AS(f.app.entries().addFromItem(kDay, f.matatu), Error);
}

TEST_CASE("a failed add leaves no trace, not even an empty day") {
    Fixture f;
    CHECK_THROWS_AS(f.app.entries().addFromItem(kDay, 9999), Error);
    CHECK_FALSE(f.app.days().savedResult(kDay).has_value());
    CHECK(f.app.days().view(kDay).entries.empty());
}

TEST_CASE("a one-off belongs to a category but not to the catalog") {
    Fixture f;
    const auto id = f.app.entries().addOneOff(kDay, f.food, "  Uncle Bill's wedding ", 500);
    const auto e = f.app.entries().find(id);
    REQUIRE(e.has_value());
    CHECK(e->isOneOff());
    CHECK_FALSE(e->itemId.has_value());
    CHECK(e->name == "Uncle Bill's wedding");
    CHECK(e->categoryName == "Food");
    CHECK(e->kind == EntryKind::Added);
    CHECK(f.app.items().list().size() == 2);                      // the catalog did not grow
}

TEST_CASE("one-off input is checked") {
    Fixture f;
    CHECK_THROWS_AS(f.app.entries().addOneOff(kDay, f.food, "  ", 100), Error);
    CHECK_THROWS_AS(f.app.entries().addOneOff(kDay, f.food, "Gift", 0), Error);
    CHECK_THROWS_AS(f.app.entries().addOneOff(kDay, 9999, "Gift", 100), Error);
    f.app.categories().remove(f.food);
    CHECK_THROWS_AS(f.app.entries().addOneOff(kDay, f.food, "Gift", 100), Error);
}

TEST_CASE("a day sums its entries and splits them by funding source") {
    Fixture f;
    f.app.entries().addFromItem(kDay, f.matatu);                          // 50 earned
    f.app.entries().addFromItem(kDay, f.milk, std::nullopt, FundedFrom::Savings);   // 60 savings
    f.app.entries().addOneOff(kDay, f.food, "Gift", 500);                 // 500 earned

    const auto v = f.app.days().view(kDay);
    CHECK(v.entries.size() == 3);
    CHECK(v.earnedTotal == 550);
    CHECK(v.savingsTotal == 60);
    CHECK(v.total() == 610);
    CHECK(f.app.days().savedResult(kDay) == 610);
}

TEST_CASE("a day with no entries is simply empty") {
    Fixture f;
    const auto v = f.app.days().view(Date::parse("2030-01-01"));
    CHECK(v.entries.empty());
    CHECK(v.total() == 0);
    CHECK_FALSE(v.calculated);
}

TEST_CASE("entries can go on past and future days") {
    Fixture f;
    const Date past = kDay.addDays(-30);
    const Date future = kDay.addDays(90);
    f.app.entries().addFromItem(past, f.matatu);
    f.app.entries().addFromItem(future, f.matatu);
    CHECK(f.app.days().view(past).total() == 50);
    CHECK(f.app.days().view(future).total() == 50);
    CHECK(f.app.days().view(kDay).total() == 0);   // other days are not affected
}

TEST_CASE("the day view groups entries in category order") {
    Fixture f;
    f.app.entries().addFromItem(kDay, f.matatu);   // Transport was added second...
    f.app.entries().addFromItem(kDay, f.milk);     // ...Food first, so Food is listed first
    const auto v = f.app.days().view(kDay);
    REQUIRE(v.entries.size() == 2);
    CHECK(v.entries[0].categoryName == "Food");
    CHECK(v.entries[1].categoryName == "Transport");
}

TEST_CASE("editing a hand-added entry keeps it 'added'") {
    Fixture f;
    const auto id = f.app.entries().addFromItem(kDay, f.matatu);
    f.app.entries().setAmount(id, 70);
    const auto e = f.app.entries().find(id);
    CHECK(e->amount == 70);
    CHECK(e->kind == EntryKind::Added);
    CHECK_FALSE(e->originalAmount.has_value());
    CHECK(f.app.days().view(kDay).total() == 70);
    CHECK(f.app.days().savedResult(kDay) == 70);

    CHECK_THROWS_AS(f.app.entries().setAmount(id, 0), Error);
    CHECK_THROWS_AS(f.app.entries().setAmount(9999, 10), Error);
}

TEST_CASE("changing where an entry is paid from moves it between the totals") {
    Fixture f;
    const auto id = f.app.entries().addFromItem(kDay, f.matatu);
    f.app.entries().setFundedFrom(id, FundedFrom::Savings);
    auto v = f.app.days().view(kDay);
    CHECK(v.earnedTotal == 0);
    CHECK(v.savingsTotal == 50);
    CHECK(v.total() == 50);
    CHECK_THROWS_AS(f.app.entries().setFundedFrom(9999, FundedFrom::Earned), Error);
}

TEST_CASE("deleting an entry updates the day") {
    Fixture f;
    const auto a = f.app.entries().addFromItem(kDay, f.matatu);
    f.app.entries().addFromItem(kDay, f.milk);
    f.app.entries().remove(a);
    CHECK(f.app.days().view(kDay).total() == 60);
    CHECK(f.app.days().savedResult(kDay) == 60);
    CHECK_FALSE(f.app.entries().find(a).has_value());
    CHECK_THROWS_AS(f.app.entries().remove(a), Error);

    const auto b = f.app.days().view(kDay).entries[0].id;
    f.app.entries().remove(b);
    CHECK(f.app.days().view(kDay).total() == 0);
    CHECK(f.app.days().savedResult(kDay) == 0);       // the day row stays, with a total of 0
}

TEST_CASE("history survives deleting the item or category it came from") {
    Fixture f;
    const auto id = f.app.entries().addFromItem(kDay, f.matatu);
    f.app.items().remove(f.matatu);
    f.app.categories().remove(f.transport);

    const auto e = f.app.entries().find(id);
    REQUIRE(e.has_value());
    CHECK(e->name == "Matatu");
    CHECK(e->categoryName == "Transport");
    CHECK(f.app.days().view(kDay).total() == 50);
}

TEST_CASE("renaming an item does not rewrite old entries, renaming a category does") {
    Fixture f;
    const auto id = f.app.entries().addFromItem(kDay, f.matatu);
    f.app.items().rename(f.matatu, "Bus");
    f.app.categories().rename(f.transport, "Commute");
    const auto e = f.app.entries().find(id);
    CHECK(e->name == "Matatu");              // saved with the entry
    CHECK(e->categoryName == "Commute");     // looked up live
}

TEST_CASE("editing an auto entry makes it 'edited' and remembers the first amount") {
    TempDb file("pesa_test_entries_auto.db");
    EntryId id = 0;
    {
        Fixture f(file.path);
        id = f.app.entries().addFromItem(kDay, f.matatu);
    }
    rawSql(file.path, "UPDATE entries SET kind = 0;");   // pretend the daily job posted it

    Pesa app(file.path);
    CHECK(app.entries().find(id)->kind == EntryKind::AutoPosted);

    app.entries().setAmount(id, 50);                      // same amount: not an edit
    CHECK(app.entries().find(id)->kind == EntryKind::AutoPosted);

    app.entries().setAmount(id, 70);
    auto e = app.entries().find(id);
    CHECK(e->kind == EntryKind::Edited);
    CHECK(e->amount == 70);
    CHECK(e->originalAmount == 50);

    app.entries().setAmount(id, 80);                      // a second edit
    e = app.entries().find(id);
    CHECK(e->kind == EntryKind::Edited);
    CHECK(e->originalAmount == 50);                       // still what the rule posted
    CHECK(app.days().view(kDay).total() == 80);
}

TEST_CASE("skipped entries are listed but never counted or edited") {
    TempDb file("pesa_test_entries_skipped.db");
    EntryId matatu = 0;
    {
        Fixture f(file.path);
        matatu = f.app.entries().addFromItem(kDay, f.matatu);
        f.app.entries().addFromItem(kDay, f.milk);
    }
    rawSql(file.path, "UPDATE entries SET kind = 3 WHERE name = 'Matatu';");   // a skip rule did this

    Pesa app(file.path);
    app.days().recalculate(kDay);                          // the repair tool
    const auto v = app.days().view(kDay);
    CHECK(v.entries.size() == 2);                          // still shown
    CHECK(v.total() == 60);                                // but not counted
    CHECK(app.days().savedResult(kDay) == 60);
    CHECK_THROWS_AS(app.entries().setAmount(matatu, 70), Error);
    CHECK_THROWS_AS(app.entries().setFundedFrom(matatu, FundedFrom::Savings), Error);
}

TEST_CASE("entries survive closing and reopening the file") {
    TempDb file("pesa_test_entries_reopen.db");
    {
        Fixture f(file.path);
        f.app.entries().addFromItem(kDay, f.matatu);
        f.app.entries().addOneOff(kDay, f.food, "Gift", 500, FundedFrom::Savings);
    }
    Pesa app(file.path);
    const auto v = app.days().view(kDay);
    CHECK(v.entries.size() == 2);
    CHECK(v.earnedTotal == 50);
    CHECK(v.savingsTotal == 500);
}
