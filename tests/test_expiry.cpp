//
// Created by lee on 11/27/26.
//
// FTracker/tests/test_expiry.cpp
#include <doctest/doctest.h>

#include <pesa/pesa.h>

using pesa::Date;
using pesa::Error;
using pesa::Pesa;
using pesa::Schedule;
using pesa::TimeOfDay;
using pesa::WeekdaySet;

TEST_CASE("a short-lived category pauses itself after its last day, and resuming keeps it") {
    Pesa app(":memory:");
    const auto trip = app.categories().add("Holiday trip");
    const auto food = app.categories().add("Food");
    app.categories().setExpiry(trip, Date::parse("2026-12-31"));
    CHECK(app.categories().find(trip)->expiresOn == Date::parse("2026-12-31"));

    CHECK(app.categories().pauseExpired(Date::parse("2026-12-31")).empty());          // still its last day
    const auto paused = app.categories().pauseExpired(Date::parse("2027-01-01"));
    REQUIRE(paused.size() == 1);
    CHECK(paused[0] == "Holiday trip");
    CHECK_FALSE(app.categories().find(trip)->isActive);
    CHECK(app.categories().find(food)->isActive);
    CHECK(app.log().list(pesa::LogFilter::Edits).size() == 1);

    app.categories().setActive(trip, true);                                           // resuming makes it permanent
    CHECK_FALSE(app.categories().find(trip)->expiresOn.has_value());
    CHECK(app.categories().pauseExpired(Date::parse("2027-06-01")).empty());

    app.categories().setExpiry(trip, Date::parse("2027-02-01"));
    app.categories().setExpiry(trip, std::nullopt);                                    // "keep forever"
    CHECK_FALSE(app.categories().find(trip)->expiresOn.has_value());
    CHECK_THROWS_AS(app.categories().setExpiry(app.categories().misc(), Date::parse("2027-01-01")), Error);
    CHECK_THROWS_AS(app.categories().setExpiry(9999, Date::parse("2027-01-01")), Error);
}

TEST_CASE("a short-lived item stops posting after its last day, even if Pesa was closed") {
    Pesa app(":memory:");
    const auto cat = app.categories().add("Food");
    const auto lunch = app.items().add({.categoryId = cat, .name = "Lunch", .defaultAmount = 250,
                                        .expiresOn = Date::parse("2026-12-31")});
    app.rules().add({.name = "Daily", .schedule = Schedule::weekly(WeekdaySet::everyDay()),
                     .effectiveFrom = Date::parse("2026-12-01"), .effectiveTo = std::nullopt, .itemIds = {lunch}});
    CHECK(app.items().find(lunch)->expiresOn == Date::parse("2026-12-31"));

    const TimeOfDay evening{21, 0};
    CHECK(app.dailyJob().run(Date::parse("2026-12-30"), evening).entries == 1);
    const auto reopened = app.dailyJob().run(Date::parse("2027-01-02"), evening);     // 31 Dec, 1 Jan, 2 Jan
    CHECK(reopened.days == 3);
    CHECK(reopened.entries == 1);                                                      // only the 31st: its last day
    CHECK(app.days().view(Date::parse("2026-12-31")).entries.size() == 1);
    CHECK(app.days().view(Date::parse("2027-01-01")).entries.empty());

    const auto paused = app.items().pauseExpired(Date::parse("2027-01-02"));
    REQUIRE(paused.size() == 1);
    CHECK_FALSE(app.items().find(lunch)->isActive);
    app.items().setActive(lunch, true);
    CHECK_FALSE(app.items().find(lunch)->expiresOn.has_value());
}

TEST_CASE("several items can be added to or removed from a rule in one step") {
    Pesa app(":memory:");
    const auto cat = app.categories().add("Transport");
    const auto a = app.items().add({.categoryId = cat, .name = "Matatu", .defaultAmount = 50});
    const auto b = app.items().add({.categoryId = cat, .name = "Tuktuk", .defaultAmount = 150});
    const auto c = app.items().add({.categoryId = cat, .name = "Boda", .defaultAmount = 100});
    const auto id = app.rules().add({.name = "Commute", .schedule = Schedule::weekly(WeekdaySet::workDays()),
                                     .effectiveFrom = Date::parse("2026-11-01"), .effectiveTo = std::nullopt,
                                     .itemIds = {a}});

    const std::vector<pesa::ItemId> more{b, c};
    app.rules().addItems(id, more);
    CHECK(app.rules().find(id)->items.size() == 3);

    const std::vector<pesa::ItemId> bad{a, 9999};
    CHECK_THROWS_AS(app.rules().addItems(id, bad), Error);                            // all or nothing
    const std::vector<pesa::ItemId> gone{b, c};
    app.rules().removeItems(id, gone);
    CHECK(app.rules().find(id)->items.size() == 1);
    const std::vector<pesa::ItemId> missing{a, b};                                    // b is already out
    CHECK_THROWS_AS(app.rules().removeItems(id, missing), Error);
    CHECK(app.rules().find(id)->items.size() == 1);                                   // a was not removed either
}