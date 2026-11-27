//
// Created by lee on 11/25/26.
//
// FTracker/tests/test_periods.cpp
#include <doctest/doctest.h>

#include <pesa/pesa.h>

using pesa::Date;
using pesa::Error;
using pesa::FundedFrom;
using pesa::IncomeMode;
using pesa::Pesa;

namespace {
    const Date kStart = Date::parse("2026-10-25");
    const Date kPayday = Date::parse("2026-11-24");

    // A salary of 70,000 due on 24 Nov, with 12,000 in hand on 25 Oct.
    struct Fixture {
        Pesa app{":memory:"};

        Fixture() {
            app.completeSetup({
                                  .mode = IncomeMode::Regular, .pay = 70000, .payday = kPayday,
                                  .startingBalance = 12000
                              }, kStart);
        }
    };
} // namespace

TEST_CASE(

    "setup opens the first period with the starting balance as earned money"
) {
    Fixture f;
    const auto s = f.app.periods().summary(kStart);
    CHECK(s.period.start == kStart);
    CHECK(s.period.isOpen());
    CHECK(s.period.expectedPayday == kPayday);
    CHECK(s.income == 12000);
    CHECK(s.earnedRemaining() == 12000);
    CHECK(f.app.periods().income().size() == 1);
}

TEST_CASE(

    "running setup again never opens a second period"
) {
    Fixture f;
    f.app.completeSetup({
                            .mode = IncomeMode::Regular, .pay = 80000, .payday = Date::parse("2026-11-30"),
                            .startingBalance = 999
                        }, kStart.addDays(1));
    const auto period = f.app.periods().current();
    REQUIRE(period.has_value());
    CHECK(period->start == kStart); // the same period
    CHECK(period->expectedPayday == Date::parse("2026-11-30"));
    CHECK(f.app.periods().summary(kStart).income == 12000); // and no second starting balance
    CHECK(f.app.settings().expectedPay() == 80000);
    CHECK_THROWS_AS(f.app.completeSetup({.mode = IncomeMode::Regular, .pay = 1, .payday = kStart}, kStart), Error);
}

TEST_CASE(

    "earned remaining is income minus earned spending, up to today"
) {
    Fixture f;
    f.app.entries().addOneOff(kStart.addDays(1), "Lunch", 500);
    f.app.entries().addOneOff(kStart.addDays(5), "Later", 900); // not happened yet
    f.app.entries().addOneOff(kStart.addDays(1), "Covered", 300, FundedFrom::Savings);

    const auto s = f.app.periods().summary(kStart.addDays(2));
    CHECK(s.spentFromEarned == 500);
    CHECK(s.spentFromSavings == 300);
    CHECK(s.earnedRemaining() == 11500);

    f.app.periods().addIncome(kStart.addDays(2), "Dad", 15000);
    CHECK(f.app.periods().summary(kStart.addDays(2)).earnedRemaining() == 26500);
}

TEST_CASE(

    "I got paid: the leftover moves to savings at once and a new period starts"
) {
    Fixture f;
    f.app.entries().addOneOff(kStart.addDays(1), "Lunch", 500);
    f.app.entries().addOneOff(kPayday, "Pay day lunch", 200); // dated on the pay day: the NEW period

    const auto r = f.app.periods().markPaid({.paidOn = kPayday});
    CHECK(r.remainder == 11500);
    CHECK(r.movedToSavings == 11500);
    CHECK(r.shortfall == 0);
    CHECK(r.leftoverPot == "Main savings");
    CHECK(r.pay == 70000);
    CHECK(r.nextPayday == Date::parse("2026-12-24"));
    CHECK(f.app.savings().total() == 11500);

    const auto history = f.app.periods().history();
    REQUIRE(history.size() == 1);
    CHECK(history[0].end == kPayday);
    CHECK(history[0].remainder == 11500);

    const auto s = f.app.periods().summary(kPayday);
    CHECK(s.period.start == kPayday);
    CHECK(s.income == 70000);
    CHECK(s.spentFromEarned == 200);
    CHECK(s.earnedRemaining() == 69800);
}

TEST_CASE(

    "the pay can be a different amount, and the leftover can go to another pot"
) {
    Fixture f;
    const auto sacco = f.app.savings().addPot("Sacco", pesa::PotKind::Tied);
    const auto r = f.app.periods().markPaid({.paidOn = kPayday, .amount = 65000, .leftoverPot = sacco});
    CHECK(r.pay == 65000);
    CHECK(f.app.savings().findPot(sacco)->balance == 12000);
    CHECK(f.app.savings().findPot(f.app.savings().mainPot())->balance == 0);
}

TEST_CASE(

    "overspending is recorded as a shortfall and nothing is taken from savings"
) {
    Fixture f;
    f.app.entries().addOneOff(kStart.addDays(1), "Big spend", 15000);
    const auto r = f.app.periods().markPaid({.paidOn = kPayday});
    CHECK(r.remainder == -3000);
    CHECK(r.shortfall == 3000);
    CHECK(r.movedToSavings == 0);
    CHECK(f.app.savings().total() == 0);
    CHECK(f.app.periods().history()[0].remainder == -3000);
}

TEST_CASE(

    "pressing I got paid twice for the same day is refused"
) {
    Fixture f;
    f.app.periods().markPaid({.paidOn = kPayday});
    CHECK_THROWS_AS(f.app.periods().markPaid({.paidOn = kPayday}), Error);
    CHECK_THROWS_AS(f.app.periods().markPaid({.paidOn = kPayday.addDays(-1)}), Error);
}

TEST_CASE(

    "getting paid early works, and the usual payday day is kept"
) {
    Fixture f;
    const auto r = f.app.periods().markPaid({.paidOn = Date::parse("2026-11-20")});
    CHECK(r.nextPayday == Date::parse("2026-12-24")); // not 20 Dec
}

TEST_CASE(

    "auto-pay closes the period once the payday has come, and only once"
) {
    Fixture f;
    CHECK(f.app.periods().autoPayIfDue(kPayday.addDays(-1)).empty());

    const auto done = f.app.periods().autoPayIfDue(kPayday);
    REQUIRE(done.size() == 1);
    CHECK(done[0].paidOn == kPayday);
    CHECK(f.app.periods().current()->start == kPayday);
    CHECK(f.app.periods().autoPayIfDue(kPayday).empty());
}

TEST_CASE(

    "auto-pay catches up every missed payday, each on its own day"
) {
    Fixture f;
    const auto done = f.app.periods().autoPayIfDue(Date::parse("2027-01-30"));
    REQUIRE(done.size() == 3); // 24 Nov, 24 Dec, 24 Jan
    CHECK(done[1].paidOn == Date::parse("2026-12-24"));
    CHECK(done[2].paidOn == Date::parse("2027-01-24"));
    CHECK(f.app.savings().total() == 12000 + 70000 + 70000);
    CHECK(f.app.periods().current()->expectedPayday == Date::parse("2027-02-24"));
}

TEST_CASE(

    "auto-pay does nothing when it is switched off or income is irregular"
) {
    Fixture f;
    f.app.settings().setAutoPayEnabled(false);
    CHECK(f.app.periods().autoPayIfDue(kPayday.addDays(10)).empty());
    CHECK(f.app.periods().summary(kPayday.addDays(3)).daysLate == 3); // the late state shows instead

    Pesa irregular(":memory:");
    irregular.completeSetup({.mode = IncomeMode::Irregular}, kStart);
    CHECK(irregular.periods().autoPayIfDue(kPayday).empty());
}

TEST_CASE(

    "salary delayed moves the payday, so auto-pay waits"
) {
    Fixture f;
    const Date today = Date::parse("2026-11-23");
    CHECK_THROWS_AS(f.app.periods().delayPayday(today, today), Error); // not after today
    f.app.periods().delayPayday(Date::parse("2026-11-27"), today);
    CHECK(f.app.periods().current()->expectedPayday == Date::parse("2026-11-27"));
    CHECK(f.app.periods().autoPayIfDue(kPayday).empty());
    CHECK(f.app.periods().autoPayIfDue(Date::parse("2026-11-27")).size() == 1);
}

TEST_CASE(

    "irregular income: no payday, no delay, and the pay amount must be given"
) {
    Pesa app(":memory:");
    app.completeSetup({.mode = IncomeMode::Irregular, .startingBalance = 5000}, kStart);
    CHECK_FALSE(app.periods().current()->expectedPayday.has_value());
    CHECK_FALSE(app.periods().summary(kPayday).daysLate.has_value());
    CHECK_THROWS_AS(app.periods().delayPayday(kPayday, kStart), Error);
    CHECK_THROWS_AS(app.periods().markPaid({.paidOn = kStart.addDays(10)}), Error); // no usual pay

    const auto r = app.periods().markPaid({.paidOn = kStart.addDays(10), .amount = 20000});
    CHECK(r.movedToSavings == 5000);
    CHECK_FALSE(r.nextPayday.has_value());
    CHECK_FALSE(app.periods().current()->expectedPayday.has_value());
}

TEST_CASE(

    "there is nothing to do without an open period"
) {
    Pesa app(":memory:");
    CHECK_FALSE(app.periods().current().has_value());
    CHECK_THROWS_AS((void)app.periods().summary(kStart), Error);
    CHECK_THROWS_AS(app.periods().addIncome(kStart, "Dad", 100), Error);
}

TEST_CASE("auto-cover is off by default, so earned simply goes below zero") {
    Fixture f;
    f.app.entries().addOneOff(kStart.addDays(1), "Laptop", 15000);
    const auto r = f.app.periods().autoCoverIfNeeded(kStart.addDays(2));
    CHECK(r.entries == 0);
    CHECK(f.app.periods().summary(kStart.addDays(2)).earnedRemaining() == -3000);
    CHECK(f.app.savings().total() == 0);
}

TEST_CASE("auto-cover pays the newest spending from savings until earned is back above zero") {
    Fixture f;
    f.app.settings().setAutoCoverEnabled(true);
    f.app.entries().addOneOff(kStart.addDays(1), "Rent", 9000);
    f.app.entries().addOneOff(kStart.addDays(2), "Laptop", 8000); // earned: 12,000 - 17,000 = -5,000

    const auto r = f.app.periods().autoCoverIfNeeded(kStart.addDays(3));
    CHECK(r.entries == 1); // the Laptop is enough
    CHECK(r.total == 8000);
    CHECK(r.savingsAfter == -8000); // Main savings can go below zero
    CHECK(f.app.savings().total() == -8000);
    CHECK(f.app.periods().summary(kStart.addDays(3)).earnedRemaining() == 3000);
    for (const auto &e: f.app.days().view(kStart.addDays(2)).entries)
        CHECK(e.fund == FundedFrom::Savings);
    for (const auto &e: f.app.days().view(kStart.addDays(1)).entries)
        CHECK(e.fund == FundedFrom::Earned);

    CHECK(f.app.periods().autoCoverIfNeeded(kStart.addDays(3)).entries == 0); // nothing left to do
}

TEST_CASE("if earned is still below zero at payday, the policy decides what happens") {
    {
        // record (the default): only noted
        Fixture f;
        f.app.entries().addOneOff(kStart.addDays(1), "Big spend", 15000);
        const auto r = f.app.periods().markPaid({.paidOn = kPayday});
        CHECK(r.shortfall == 3000);
        CHECK(r.settledFromSavings == 0);
        CHECK(r.carriedForward == 0);
        CHECK(f.app.savings().total() == 0);
    }
    {
        // take it from savings
        Fixture f;
        f.app.settings().setShortfallPolicy(pesa::ShortfallPolicy::TakeFromSavings);
        f.app.entries().addOneOff(kStart.addDays(1), "Big spend", 15000);
        const auto r = f.app.periods().markPaid({.paidOn = kPayday});
        CHECK(r.settledFromSavings == 3000);
        CHECK(f.app.savings().total() == -3000);
        CHECK(f.app.periods().summary(kPayday).earnedRemaining() == 70000); // the new period starts clean
    }
    {
        // carry it forward
        Fixture f;
        f.app.settings().setShortfallPolicy(pesa::ShortfallPolicy::CarryForward);
        f.app.entries().addOneOff(kStart.addDays(1), "Big spend", 15000);
        const auto r = f.app.periods().markPaid({.paidOn = kPayday});
        CHECK(r.carriedForward == 3000);
        CHECK(f.app.savings().total() == 0);
        CHECK(f.app.periods().summary(kPayday).earnedRemaining() == 67000); // 70,000 - 3,000
        const auto day = f.app.days().view(kPayday);
        REQUIRE(day.entries.size() == 1);
        CHECK(day.entries[0].name == "Overspent last period");
    }
}
