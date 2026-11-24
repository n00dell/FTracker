//
// Created by lee on 11/24/26.
//
// FTracker/tests/test_settings.cpp
#include <doctest/doctest.h>

#include <pesa/pesa.h>

using pesa::Date;
using pesa::Error;
using pesa::Money;
using pesa::Pesa;

TEST_CASE("a new database has not been set up and auto-post is on") {
    Pesa app(":memory:");
    CHECK_FALSE(app.settings().setupDone());
    CHECK_FALSE(app.settings().expectedPay().has_value());
    CHECK_FALSE(app.settings().expectedPayday().has_value());
    CHECK(app.settings().autoPostEnabled());
}

TEST_CASE("setup saves the pay and the payday") {
    Pesa app(":memory:");
    app.settings().completeSetup(70000, Date::parse("2026-12-24"));
    CHECK(app.settings().setupDone());
    CHECK(app.settings().expectedPay() == 70000);
    CHECK(app.settings().expectedPayday() == Date::parse("2026-12-24"));
}

TEST_CASE("setup with no pay is allowed, and a payday without a pay is dropped") {
    Pesa app(":memory:");
    app.settings().completeSetup(std::nullopt, Date::parse("2026-12-24"));
    CHECK(app.settings().setupDone());
    CHECK_FALSE(app.settings().expectedPay().has_value());
    CHECK_FALSE(app.settings().expectedPayday().has_value());
}

TEST_CASE("a bad pay is refused and nothing is saved") {
    Pesa app(":memory:");
    CHECK_THROWS_AS(app.settings().completeSetup(Money{0}, std::nullopt), Error);
    CHECK_FALSE(app.settings().setupDone());
}

TEST_CASE("auto-post can be switched off and on") {
    Pesa app(":memory:");
    app.settings().setAutoPostEnabled(false);
    CHECK_FALSE(app.settings().autoPostEnabled());
    app.settings().setAutoPostEnabled(true);
    CHECK(app.settings().autoPostEnabled());
}