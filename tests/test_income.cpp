//
// Created by lee on 11/27/26.
//
// FTracker/tests/test_income.cpp
#include <doctest/doctest.h>

#include <pesa/pesa.h>

using pesa::Date;
using pesa::Error;
using pesa::Frequency;
using pesa::IncomeMode;
using pesa::Pesa;

namespace {

const Date kStart = Date::parse("2026-10-25");
const Date kPayday = Date::parse("2026-11-24");

// A salary of 70,000 due on 24 Nov, from 25 Oct.
struct Fixture {
    Pesa app{":memory:"};
    Fixture() {
        app.completeSetup({.mode = IncomeMode::Regular, .pay = 70000, .payday = kPayday}, kStart);
    }
};

} // namespace

TEST_CASE("setup starts the pay history") {
    Fixture f;
    const auto history = f.app.income().payHistory();
    REQUIRE(history.size() == 1);
    CHECK(history[0].amount == 70000);
    CHECK(history[0].from == kStart);
    CHECK_FALSE(history[0].to.has_value());
    CHECK(f.app.settings().expectedPay() == 70000);
}

TEST_CASE("a raise is a new row, the old amount ends the day before, and past days keep what they had") {
    Fixture f;
    f.app.income().setPay(75000, Date::parse("2026-12-01"));
    const auto history = f.app.income().payHistory();
    REQUIRE(history.size() == 2);
    CHECK(history[0].to == Date::parse("2026-11-30"));
    CHECK(history[1].amount == 75000);

    CHECK(f.app.income().payOn(kPayday) == 70000);
    CHECK(f.app.income().payOn(Date::parse("2026-12-01")) == 75000);
    CHECK(f.app.income().payOn(Date::parse("2026-10-01")) == 70000);             // before the first row: the first amount
    CHECK(f.app.settings().expectedPay() == 75000);                              // the newest amount

    f.app.income().setPay(76000, Date::parse("2026-12-01"));                     // the same day: a correction
    CHECK(f.app.income().payHistory().size() == 2);
    CHECK(f.app.income().payOn(Date::parse("2026-12-15")) == 76000);

    CHECK_THROWS_AS(f.app.income().setPay(1000, Date::parse("2026-11-15")), Error);   // earlier than the newest row
    CHECK_THROWS_AS(f.app.income().setPay(0, Date::parse("2026-12-20")), Error);
}

TEST_CASE("I got paid adds the pay of the day you were paid") {
    Fixture f;
    f.app.income().setPay(75000, Date::parse("2026-12-01"));
    CHECK(f.app.periods().markPaid({.paidOn = kPayday}).pay == 70000);
    CHECK(f.app.periods().markPaid({.paidOn = Date::parse("2026-12-24")}).pay == 75000);
}

TEST_CASE("income you expect counts for nothing until it arrives") {
    Fixture f;
    const auto id = f.app.income().addSource({.name = "Dad", .usualAmount = 3000, .frequency = Frequency::Monthly,
                                              .nextExpected = Date::parse("2026-11-05")});
    CHECK(f.app.periods().summary(Date::parse("2026-11-05")).income == 0);
    CHECK(f.app.income().sources().size() == 1);

    CHECK(f.app.income().due(Date::parse("2026-11-04")).empty());
    REQUIRE(f.app.income().due(Date::parse("2026-11-05")).size() == 1);

    f.app.income().notYet(id, Date::parse("2026-11-05"));                       // "not yet": asked again tomorrow
    CHECK(f.app.income().due(Date::parse("2026-11-05")).empty());
    CHECK(f.app.income().due(Date::parse("2026-11-06")).size() == 1);

    const auto r = f.app.income().received(id, std::nullopt, Date::parse("2026-11-06"));
    CHECK(r.amount == 3000);
    CHECK(r.nextExpected == Date::parse("2026-12-05"));
    CHECK_FALSE(r.removed);
    CHECK(f.app.periods().summary(Date::parse("2026-11-06")).income == 3000);
    CHECK(f.app.periods().income().back().label == "Dad");
    CHECK(f.app.income().due(Date::parse("2026-11-06")).empty());
}

TEST_CASE("late income steps on to a date in the future, keeping the day of the month") {
    Fixture f;
    const auto id = f.app.income().addSource({.name = "Dad", .usualAmount = 3000, .frequency = Frequency::Monthly,
                                              .nextExpected = Date::parse("2026-11-05")});
    const auto r = f.app.income().received(id, 2500, Date::parse("2026-12-20"));   // a smaller amount, very late
    CHECK(r.amount == 2500);
    CHECK(r.nextExpected == Date::parse("2027-01-05"));                            // not 5 Dec, which is already past
}

TEST_CASE("income can go into a pot instead of earned") {
    Fixture f;
    const auto sacco = f.app.savings().addPot("Sacco", pesa::PotKind::Tied);
    const auto id = f.app.income().addSource({.name = "Dividends", .usualAmount = 4500, .frequency = Frequency::Quarterly,
                                              .nextExpected = Date::parse("2026-12-15"), .potId = sacco});
    CHECK(f.app.income().findSource(id)->goesTo == "Sacco");

    const auto r = f.app.income().received(id, std::nullopt, Date::parse("2026-12-15"));
    CHECK(r.goesTo == "Sacco");
    CHECK(r.nextExpected == Date::parse("2027-03-15"));
    CHECK(f.app.savings().findPot(sacco)->balance == 4500);
    CHECK(f.app.periods().summary(Date::parse("2026-12-15")).income == 0);         // not earned
}

TEST_CASE("a one-off is removed once it arrived, and skipping is for income that repeats") {
    Fixture f;
    const auto bonus = f.app.income().addSource({.name = "Bonus", .usualAmount = 20000, .frequency = Frequency::OneOff,
                                                 .nextExpected = Date::parse("2026-11-10")});
    CHECK_THROWS_AS(f.app.income().skip(bonus, Date::parse("2026-11-10")), Error);
    const auto r = f.app.income().received(bonus, std::nullopt, Date::parse("2026-11-10"));
    CHECK(r.removed);
    CHECK_FALSE(f.app.income().findSource(bonus).has_value());
    CHECK(f.app.income().sources().empty());

    const auto rent = f.app.income().addSource({.name = "Rent", .usualAmount = 9000, .frequency = Frequency::Monthly,
                                                .nextExpected = Date::parse("2026-11-01")});
    CHECK(f.app.income().skip(rent, Date::parse("2026-11-02")) == Date::parse("2026-12-01"));
    const auto noDate = f.app.income().addSource({.name = "Tips", .usualAmount = 500, .frequency = Frequency::Monthly});
    CHECK_THROWS_AS(f.app.income().skip(noDate, Date::parse("2026-11-02")), Error);   // nothing expected
    CHECK(f.app.income().due(Date::parse("2027-01-01")).size() == 1);                 // "no plan yet": Pesa never asks (only Rent)
}

TEST_CASE("income that comes with your pay is added to the new period when you press paid") {
    Fixture f;
    const auto sacco = f.app.savings().addPot("Sacco", pesa::PotKind::Tied);
    f.app.income().addSource({.name = "Dad allowance", .usualAmount = 3000, .frequency = Frequency::WithPay});
    f.app.income().addSource({.name = "Contribution", .usualAmount = 1000, .frequency = Frequency::WithPay, .potId = sacco});
    CHECK_THROWS_AS(f.app.income().addSource({.name = "X", .usualAmount = 1, .frequency = Frequency::WithPay,
                                              .nextExpected = kPayday}), Error);
    CHECK(f.app.income().due(Date::parse("2027-06-01")).empty());                     // it never asks

    const auto r = f.app.periods().markPaid({.paidOn = kPayday});
    REQUIRE(r.withPay.size() == 2);
    CHECK(r.withPay[0].name == "Dad allowance");
    CHECK(r.withPay[1].goesTo == "Sacco");
    CHECK(f.app.periods().summary(kPayday).income == 70000 + 3000);                   // the pot part is not earned
    CHECK(f.app.savings().findPot(sacco)->balance == 1000);
}

TEST_CASE("expected income is validated, and can be removed") {
    Fixture f;
    CHECK_THROWS_AS(f.app.income().addSource({.name = "  ", .usualAmount = 100}), Error);
    CHECK_THROWS_AS(f.app.income().addSource({.name = "Dad", .usualAmount = 0}), Error);
    CHECK_THROWS_AS(f.app.income().addSource({.name = "Dad", .usualAmount = 100, .potId = 9999}), Error);
    const auto id = f.app.income().addSource({.name = "Dad", .usualAmount = 100});
    CHECK_THROWS_AS(f.app.income().addSource({.name = "dad", .usualAmount = 100}), Error);   // names are unique
    CHECK_THROWS_AS(f.app.income().received(9999, std::nullopt, kStart), Error);

    f.app.income().removeSource(id);
    CHECK(f.app.income().sources().empty());
    CHECK_THROWS_AS(f.app.income().removeSource(id), Error);
    CHECK_NOTHROW(f.app.income().addSource({.name = "Dad", .usualAmount = 100}));            // the name is free again
}