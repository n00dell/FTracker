// FTracker/tests/test_savings.cpp
#include <doctest/doctest.h>

#include <pesa/pesa.h>

using pesa::Date;
using pesa::Error;
using pesa::FundedFrom;
using pesa::IncomeMode;
using pesa::LedgerKind;
using pesa::Pesa;

namespace {
const Date kDay = Date::parse("2026-11-23");
}

TEST_CASE("a new database has the built-in Main savings pot at 0") {
    Pesa app(":memory:");
    const auto pots = app.savings().pots();
    REQUIRE(pots.size() == 1);
    CHECK(pots[0].name == "Main savings");
    CHECK(pots[0].isMain);
    CHECK(pots[0].balance == 0);
    CHECK(pots[0].id == app.savings().mainPot());
    CHECK(app.savings().total() == 0);
}

TEST_CASE("pots have unique names, and balances are the sum of their ledger lines") {
    Pesa app(":memory:");
    const auto sacco = app.savings().addPot("  Sacco ", pesa::PotKind::Tied);
    CHECK_THROWS_AS(app.savings().addPot("sacco"), Error);
    CHECK_THROWS_AS(app.savings().addPot("   "), Error);

    app.savings().add(kDay, app.savings().mainPot(), 1000, "Gift");
    app.savings().add(kDay, sacco, 250, "Contribution");
    CHECK(app.savings().findPot(sacco)->balance == 250);
    CHECK(app.savings().findPot(sacco)->kind == pesa::PotKind::Tied);
    CHECK(app.savings().total() == 1250);

    const auto ledger = app.savings().ledger();
    REQUIRE(ledger.size() == 2);
    CHECK(ledger[0].kind == LedgerKind::AddedToSavings);
    CHECK(ledger[0].note == "Gift");
    CHECK_THROWS_AS(app.savings().add(kDay, app.savings().mainPot(), 0, "x"), Error);
    CHECK_THROWS_AS(app.savings().add(kDay, 9999, 10, "x"), Error);
}

TEST_CASE("moving savings to earned takes it from the pot and adds this period's income") {
    Pesa app(":memory:");
    CHECK_THROWS_AS(app.savings().moveToEarned(kDay, app.savings().mainPot(), 100), Error);   // no period yet

    app.completeSetup({.mode = IncomeMode::Irregular, .startingBalance = 1000}, kDay);
    app.savings().add(kDay, app.savings().mainPot(), 5000, "Gift");
    app.savings().moveToEarned(kDay, app.savings().mainPot(), 2000);

    CHECK(app.savings().total() == 3000);
    CHECK(app.periods().summary(kDay).income == 3000);
    const auto income = app.periods().income();
    REQUIRE(income.size() == 2);
    CHECK(income[1].label == "From Main savings");
}

TEST_CASE("an entry paid from savings keeps its ledger line in step with the entry") {
    Pesa app(":memory:");
    app.savings().add(kDay, app.savings().mainPot(), 1000, "Gift");

    const auto id = app.entries().addOneOff(kDay, "Barber", 250, FundedFrom::Savings);
    CHECK(app.savings().total() == 750);                       // created paid from savings

    app.entries().setFundedFrom(id, FundedFrom::Earned);
    CHECK(app.savings().total() == 1000);                      // back to earned: refunded
    app.entries().setFundedFrom(id, FundedFrom::Savings);
    CHECK(app.savings().total() == 750);

    app.entries().setAmount(id, 300);
    CHECK(app.savings().total() == 700);                       // a new amount changes the line

    app.entries().remove(id);
    CHECK(app.savings().total() == 1000);                      // deleting refunds the pot
    CHECK(app.savings().ledger().size() == 1);
}

TEST_CASE("cover from savings switches several entries at once, and skips covered ones") {
    Pesa app(":memory:");
    const auto a = app.entries().addOneOff(kDay, "A", 100);
    const auto b = app.entries().addOneOff(kDay, "B", 200);
    const std::vector<pesa::EntryId> ids{a, b};

    CHECK(app.entries().coverFromSavings(ids) == 300);
    CHECK(app.savings().total() == -300);                      // allowed to go below 0; the UI warns
    CHECK(app.entries().find(a)->fund == FundedFrom::Savings);
    CHECK(app.entries().coverFromSavings(ids) == 0);           // nothing new to cover
    CHECK(app.savings().total() == -300);

    const std::vector<pesa::EntryId> missing{9999};
    CHECK_THROWS_AS(app.entries().coverFromSavings(missing), Error);
}

TEST_CASE("a failed cover leaves everything as it was") {
    Pesa app(":memory:");
    const auto a = app.entries().addOneOff(kDay, "A", 100);
    const std::vector<pesa::EntryId> ids{a, 9999};
    CHECK_THROWS_AS(app.entries().coverFromSavings(ids), Error);
    CHECK(app.entries().find(a)->fund == FundedFrom::Earned);  // rolled back, not half covered
    CHECK(app.savings().total() == 0);
}