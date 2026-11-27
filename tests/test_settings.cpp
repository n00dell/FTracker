// FTracker/tests/test_settings.cpp
#include <doctest/doctest.h>

#include <pesa/pesa.h>

using pesa::Date;
using pesa::Error;
using pesa::IncomeMode;
using pesa::Pesa;

TEST_CASE(

    "a new database has not been set up and the switches default to on"
) {
    Pesa app(":memory:");
    CHECK_FALSE(app.settings().setupDone());
    CHECK_FALSE(app.settings().expectedPay().has_value());
    CHECK(app.settings().autoPostEnabled());
    CHECK(app.settings().autoPayEnabled());
    CHECK(app.settings().postAfter() == pesa::TimeOfDay{20, 0});
}

TEST_CASE(

    "regular setup saves the mode and the pay"
) {
    Pesa app(":memory:");
    app.settings().completeSetup({.mode = IncomeMode::Regular, .pay = 70000, .payday = Date::parse("2026-12-24")});
    CHECK(app.settings().setupDone());
    CHECK(app.settings().incomeMode() == IncomeMode::Regular);
    CHECK(app.settings().expectedPay() == 70000);
}

TEST_CASE(

    "a regular income needs a pay and a payday, and a bad pay saves nothing"
) {
    Pesa app(":memory:");
    CHECK_THROWS_AS(app.settings().completeSetup({.mode = IncomeMode::Regular}), Error);
    CHECK_THROWS_AS(app.settings().completeSetup({.mode = IncomeMode::Regular, .pay = 70000}), Error);
    CHECK_THROWS_AS(app.settings().completeSetup({.mode = IncomeMode::Regular, .pay = 0,
                        .payday = Date::parse("2026-12-24")}), Error);
    CHECK_FALSE(app.settings().setupDone());
}

TEST_CASE(

    "an irregular income has no pay"
) {
    Pesa app(":memory:");
    app.settings().completeSetup({.mode = IncomeMode::Irregular, .pay = 5000});
    CHECK(app.settings().incomeMode() == IncomeMode::Irregular);
    CHECK_FALSE(app.settings().expectedPay().has_value());
}

TEST_CASE(

    "the switches and the post-after time can be changed"
) {
    Pesa app(":memory:");
    app.settings().setAutoPostEnabled(false);
    app.settings().setAutoPayEnabled(false);
    CHECK_FALSE(app.settings().autoPostEnabled());
    CHECK_FALSE(app.settings().autoPayEnabled());

    app.settings().setPostAfter(pesa::TimeOfDay::parse("7:05"));
    CHECK(app.settings().postAfter().toString() == "07:05");
    CHECK_THROWS_AS((void)pesa::TimeOfDay::parse("25:00"), Error);
    CHECK_THROWS_AS((void)pesa::TimeOfDay::parse("noon"), Error);
}

TEST_CASE("auto-cover is off and the payday policy is 'record' by default, and both can change") {
    Pesa app(":memory:");
    CHECK_FALSE(app.settings().autoCoverEnabled());
    CHECK(app.settings().shortfallPolicy() == pesa::ShortfallPolicy::Record);

    app.settings().setAutoCoverEnabled(true);
    app.settings().setShortfallPolicy(pesa::ShortfallPolicy::CarryForward);
    CHECK(app.settings().autoCoverEnabled());
    CHECK(app.settings().shortfallPolicy() == pesa::ShortfallPolicy::CarryForward);
    app.settings().setShortfallPolicy(pesa::ShortfallPolicy::TakeFromSavings);
    CHECK(app.settings().shortfallPolicy() == pesa::ShortfallPolicy::TakeFromSavings);
}
