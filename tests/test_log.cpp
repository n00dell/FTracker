//
// Created by lee on 11/25/26.
//
// FTracker/tests/test_log.cpp
#include <doctest/doctest.h>

#include <pesa/pesa.h>

using pesa::Date;
using pesa::Error;
using pesa::FundedFrom;
using pesa::IncomeMode;
using pesa::LogFilter;
using pesa::LogKind;
using pesa::Pesa;
using pesa::Schedule;
using pesa::TimeOfDay;
using pesa::WeekdaySet;

namespace {

const Date kDay = Date::parse("2026-11-23");   // a Monday

// Is there a line of this kind whose text contains `part`?
bool has(const std::vector<pesa::LogEvent>& events, LogKind kind, const std::string& part) {
    for (const auto& e : events) {
        if (e.kind == kind && e.message.find(part) != std::string::npos) return true;
    }
    return false;
}

} // namespace

TEST_CASE("a new database has an empty log") {
    Pesa app(":memory:");
    CHECK(app.log().list().empty());
    CHECK(app.log().issueCount() == 0);
    CHECK_THROWS_AS((void)app.log().list(LogFilter::All, 0), Error);
}

TEST_CASE("adding, editing and deleting entries are logged, newest first") {
    Pesa app(":memory:");
    const auto id = app.entries().addOneOff(kDay, "Gift", 500);
    app.entries().setAmount(id, 700);
    app.entries().remove(id);

    const auto events = app.log().list();
    REQUIRE(events.size() == 3);
    CHECK(events[0].kind == LogKind::Manual);
    CHECK(events[0].message.find("Deleted Gift") != std::string::npos);
    CHECK(events[1].kind == LogKind::Edit);
    CHECK(events[1].message.find("500 -> 700") != std::string::npos);
    CHECK(events[2].message.find("Added one-off Gift") != std::string::npos);
    CHECK(events[2].time.size() == 5);                // "HH:MM"
}

TEST_CASE("an edit that changes nothing is not logged") {
    Pesa app(":memory:");
    const auto id = app.entries().addOneOff(kDay, "Gift", 500);
    app.entries().setAmount(id, 500);
    app.entries().setFundedFrom(id, FundedFrom::Earned);
    CHECK(app.log().list().size() == 1);              // only the "Added" line
}

TEST_CASE("paying from savings, refunds and covering are logged as savings moves") {
    Pesa app(":memory:");
    const auto id = app.entries().addOneOff(kDay, "Barber", 250, FundedFrom::Savings);
    CHECK(has(app.log().list(LogFilter::Savings), LogKind::Savings, "paid from Main savings"));

    app.entries().remove(id);
    CHECK(has(app.log().list(), LogKind::Manual, "Deleted Barber"));
    CHECK(has(app.log().list(LogFilter::Savings), LogKind::Savings, "returned to Main savings"));
}

TEST_CASE("covering several entries writes one line, not one per entry") {
    Pesa app(":memory:");
    const auto a = app.entries().addOneOff(kDay, "A", 100);
    const auto b = app.entries().addOneOff(kDay, "B", 200);
    const std::vector<pesa::EntryId> ids{a, b};
    app.entries().coverFromSavings(ids);

    const auto savings = app.log().list(LogFilter::Savings);
    REQUIRE(savings.size() == 1);
    CHECK(savings[0].message.find("Covered 2 entries (KES 300)") != std::string::npos);
}

TEST_CASE("a change that fails leaves no log line behind") {
    Pesa app(":memory:");
    const auto a = app.entries().addOneOff(kDay, "A", 100);
    const std::vector<pesa::EntryId> ids{a, 9999};
    CHECK_THROWS_AS(app.entries().coverFromSavings(ids), Error);
    CHECK(app.log().list().size() == 1);              // still only "Added"
}

TEST_CASE("filters show one kind, and the limit keeps the newest") {
    Pesa app(":memory:");
    const auto a = app.entries().addOneOff(kDay, "One", 100);
    app.entries().addOneOff(kDay, "Two", 100);
    app.entries().addOneOff(kDay, "Three", 100);
    app.entries().setAmount(a, 150);

    CHECK(app.log().list(LogFilter::Edits).size() == 1);
    CHECK(app.log().list(LogFilter::Issues).empty());
    const auto newest = app.log().list(LogFilter::All, 2);
    REQUIRE(newest.size() == 2);
    CHECK(newest[0].kind == LogKind::Edit);
    CHECK(newest[1].message.find("Three") != std::string::npos);
}

TEST_CASE("savings moves and income are logged") {
    Pesa app(":memory:");
    app.completeSetup({.mode = IncomeMode::Irregular, .startingBalance = 1000}, kDay);
    app.savings().addPot("Sacco");
    app.savings().add(kDay, app.savings().mainPot(), 5000, "Gift");
    app.savings().moveToEarned(kDay, app.savings().mainPot(), 2000);
    app.periods().addIncome(kDay, "Dad", 15000);

    const auto events = app.log().list();
    CHECK(has(events, LogKind::Pay, "First period opened on 2026-11-23 with KES 1,000"));
    CHECK(has(events, LogKind::Savings, "New pot: Sacco"));
    CHECK(has(events, LogKind::Savings, "Added KES 5,000 to Main savings: Gift"));
    CHECK(has(events, LogKind::Savings, "Moved KES 2,000 from Main savings to earned"));
    CHECK(has(events, LogKind::Pay, "Income: Dad, KES 15,000"));
}

TEST_CASE("getting paid, auto-pay and a delayed salary are logged") {
    Pesa app(":memory:");
    app.completeSetup({.mode = IncomeMode::Regular, .pay = 70000, .payday = Date::parse("2026-11-24"),
                       .startingBalance = 12000}, Date::parse("2026-10-25"));

    app.periods().delayPayday(Date::parse("2026-11-26"), Date::parse("2026-11-23"));
    CHECK(has(app.log().list(), LogKind::Pay, "Salary delayed: expected payday moved from 2026-11-24 to 2026-11-26"));

    app.periods().autoPayIfDue(Date::parse("2026-11-26"));
    CHECK(has(app.log().list(), LogKind::Pay, "auto-pay"));
    CHECK(has(app.log().list(), LogKind::Savings, "Leftover KES 12,000 moved to Main savings"));

    app.periods().markPaid({.paidOn = Date::parse("2026-12-20")});
    CHECK(has(app.log().list(), LogKind::Pay, "Got paid on 2026-12-20"));
}

TEST_CASE("the daily job logs what it posted, catch-ups, and a stuck clock once") {
    Pesa app(":memory:");
    const auto cat = app.categories().add("Transport");
    const auto matatu = app.items().add({.categoryId = cat, .name = "Matatu", .defaultAmount = 50});
    app.rules().add({.name = "Commute", .schedule = Schedule::weekly(WeekdaySet::workDays()),
                     .effectiveFrom = Date::parse("2026-11-01"), .effectiveTo = std::nullopt, .itemIds = {matatu}});
    const TimeOfDay evening{21, 0};

    app.dailyJob().run(kDay, evening);
    CHECK(has(app.log().list(LogFilter::Auto), LogKind::Rule, "Auto-posted Matatu: KES 50 for 2026-11-23"));
    const auto before = app.log().list().size();
    app.dailyJob().run(kDay, evening);                                    // nothing new to post
    CHECK(app.log().list().size() == before);

    app.dailyJob().run(Date::parse("2026-11-30"), evening);
    CHECK(has(app.log().list(), LogKind::Rule, "Catch-up: posted 7 days (2026-11-24 to 2026-11-30)"));

    app.dailyJob().run(Date::parse("2026-11-20"), evening);               // the computer's date went backwards
    app.dailyJob().run(Date::parse("2026-11-20"), evening);               // ...and again on the next launch
    CHECK(app.log().issueCount() == 1);
    CHECK(has(app.log().list(LogFilter::Issues), LogKind::Issue, "Posting paused"));
}