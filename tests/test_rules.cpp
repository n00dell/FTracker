//
// Created by lee on 11/24/26.
//
// FTracker/tests/test_rules.cpp
#include <doctest/doctest.h>

#include <pesa/pesa.h>

using pesa::Date;
using pesa::Error;
using pesa::Pesa;
using pesa::Schedule;
using pesa::WeekdaySet;

TEST_CASE("schedules are validated when built") {
    CHECK_THROWS_AS((void)Schedule::weekly(WeekdaySet{}), Error);
    CHECK_THROWS_AS((void)Schedule::monthly(0), Error);
    CHECK_THROWS_AS((void)Schedule::monthly(32), Error);
}

TEST_CASE("a monthly rule on the 31st fires on the last day of shorter months") {
    const auto schedule = Schedule::monthly(31);
    CHECK(schedule.occursOn(Date::parse("2027-02-28")));
    CHECK_FALSE(schedule.occursOn(Date::parse("2027-02-27")));
    CHECK(schedule.occursOn(Date::parse("2026-12-31")));
}

TEST_CASE("a rule applies only inside its dates, and only while active") {
    Pesa app(":memory:");
    const auto cat = app.categories().add("Transport");
    const auto item = app.items().add({.categoryId = cat, .name = "Matatu", .defaultAmount = 50});
    const auto id = app.rules().add({.name = "Commute",
                                     .schedule = Schedule::weekly(WeekdaySet::workDays()),
                                     .effectiveFrom = Date::parse("2026-11-23"),
                                     .effectiveTo = Date::parse("2026-11-27"),
                                     .itemIds = {item}});

    auto rule = app.rules().find(id);
    REQUIRE(rule.has_value());
    CHECK(rule->items.size() == 1);
    CHECK(rule->appliesOn(Date::parse("2026-11-23")));         // Monday, first day
    CHECK_FALSE(rule->appliesOn(Date::parse("2026-11-16")));   // before it starts
    CHECK_FALSE(rule->appliesOn(Date::parse("2026-11-30")));   // after it ended
    CHECK_FALSE(rule->appliesOn(Date::parse("2026-11-28")));   // Saturday

    app.rules().setActive(id, false);
    CHECK_FALSE(app.rules().find(id)->appliesOn(Date::parse("2026-11-23")));
}

TEST_CASE("rule names are unique, and a rule cannot end before it starts") {
    Pesa app(":memory:");
    const auto cat = app.categories().add("Transport");
    const auto item = app.items().add({.categoryId = cat, .name = "Matatu", .defaultAmount = 50});
    const pesa::NewRule base{.name = "Commute", .schedule = Schedule::weekly(WeekdaySet::workDays()),
                             .effectiveFrom = Date::parse("2026-11-23"), .effectiveTo = std::nullopt,
                             .itemIds = {item}};
    const auto id = app.rules().add(base);
    CHECK_THROWS_AS(app.rules().add(base), Error);
    CHECK_THROWS_AS(app.rules().endOn(id, Date::parse("2026-11-01")), Error);
    CHECK_NOTHROW(app.rules().endOn(id, Date::parse("2026-12-31")));
}

TEST_CASE("deleting an item removes it from its rules") {
    Pesa app(":memory:");
    const auto cat = app.categories().add("Transport");
    const auto item = app.items().add({.categoryId = cat, .name = "Matatu", .defaultAmount = 50});
    const auto id = app.rules().add({.name = "Commute", .schedule = Schedule::weekly(WeekdaySet::workDays()),
                                     .effectiveFrom = Date::parse("2026-11-23"), .effectiveTo = std::nullopt,
                                     .itemIds = {item}});
    app.items().remove(item);
    CHECK(app.rules().find(id)->items.empty());
}