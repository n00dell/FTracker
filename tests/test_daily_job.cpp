//
// Created by lee on 11/24/26.
//
// FTracker/tests/test_daily_job.cpp
#include <doctest/doctest.h>

#include <pesa/pesa.h>

using pesa::Date;
using pesa::EntryKind;
using pesa::FundedFrom;
using pesa::Pesa;
using pesa::Schedule;
using pesa::TimeOfDay;
using pesa::WeekdaySet;

namespace {
    const Date kMon = Date::parse("2026-11-23"); // a Monday
    const TimeOfDay kMorning{9, 0};
    const TimeOfDay kEvening{21, 0}; // after the default "post after" of 20:00

    // One item (Matatu, 50) posted on weekdays since the start of November.
    struct Fixture {
        Pesa app{":memory:"};
        pesa::ItemId matatu = 0;
        pesa::RuleId rule = 0;

        Fixture() {
            const auto cat = app.categories().add("Transport");
            matatu = app.items().add({.categoryId = cat, .name = "Matatu", .defaultAmount = 50});
            rule = app.rules().add({
                .name = "Commute", .schedule = Schedule::weekly(WeekdaySet::workDays()),
                .effectiveFrom = Date::parse("2026-11-01"), .effectiveTo = std::nullopt,
                .itemIds = {matatu}
            });
        }
    };
} // namespace

TEST_CASE (

"a first run posts only the latest postable day, as auto entries"
)
 {
    Fixture f;
    const auto r = f.app.dailyJob().run(kMon, kEvening);
    CHECK(r.ran);
    CHECK(r.days == 1);
    CHECK(r.entries == 1);
    CHECK(r.total == 50);
    CHECK(r.from == kMon);
    CHECK(r.to == kMon);
    CHECK(f.app.dailyJob().lastPosted() == kMon);

    const auto day = f.app.days().view(kMon);
    REQUIRE(day.entries.size() == 1);
    CHECK(day.calculated);
    CHECK(day.entries[0].kind == EntryKind::AutoPosted);
    CHECK(day.entries[0].fund == FundedFrom::Earned);
    CHECK(day.entries[0].ruleId == f.rule);
    CHECK(day.entries[0].itemId == f.matatu);
    CHECK(day.entries[0].amount == 50);
    CHECK(f.app.days().view(kMon.addDays(-3)).entries.empty());   // history is not rewritten
}

TEST_CASE (

"before the post-after time only earlier days post"
)
 {
    Fixture f;
    const auto morning = f.app.dailyJob().run(kMon, kMorning);    // handles Sunday: nothing to post
    CHECK(morning.days == 1);
    CHECK(morning.entries == 0);
    CHECK(f.app.dailyJob().lastPosted() == kMon.previousDay());
    CHECK(f.app.days().view(kMon).entries.empty());               // today has not been posted

    const auto evening = f.app.dailyJob().run(kMon, kEvening);    // now today is due
    CHECK(evening.entries == 1);
    CHECK(f.app.days().view(kMon).entries.size() == 1);
}

TEST_CASE (

"the post-after time can be changed"
)
 {
    Fixture f;
    f.app.settings().setPostAfter({0, 0});                        // post today straight away
    CHECK(f.app.dailyJob().run(kMon, {0, 5}).entries == 1);
}

TEST_CASE (

"missed days are caught up, weekends included"
)
 {
    Fixture f;
    f.app.dailyJob().run(kMon, kEvening);
    const auto r = f.app.dailyJob().run(Date::parse("2026-11-30"), kEvening);   // the next Monday
    CHECK(r.days == 7);          // Tue 24th ... Mon 30th
    CHECK(r.entries == 5);       // Tue, Wed, Thu, Fri, Mon (Sat and Sun do not post)
    CHECK(r.total == 250);
    CHECK(r.from == Date::parse("2026-11-24"));
    CHECK(r.to == Date::parse("2026-11-30"));
}

TEST_CASE (

"running twice never posts twice"
)
 {
    Fixture f;
    f.app.dailyJob().run(kMon, kEvening);
    const auto again = f.app.dailyJob().run(kMon, kEvening);
    CHECK(again.days == 0);
    CHECK(again.entries == 0);
    CHECK(f.app.days().view(kMon).entries.size() == 1);
}

TEST_CASE (

"a computer clock that is behind pauses the job"
)
 {
    Fixture f;
    f.app.dailyJob().run(kMon, kEvening);
    const auto r = f.app.dailyJob().run(kMon.addDays(-3), kEvening);
    CHECK(r.clockBehind);
    CHECK_FALSE(r.ran);
    CHECK(f.app.dailyJob().lastPosted() == kMon);
    CHECK(f.app.days().view(kMon.addDays(-3)).entries.empty());
}

TEST_CASE (

"switching auto-post off posts nothing and remembers nothing"
)
 {
    Fixture f;
    f.app.settings().setAutoPostEnabled(false);
    const auto r = f.app.dailyJob().run(kMon, kEvening);
    CHECK_FALSE(r.ran);
    CHECK_FALSE(f.app.dailyJob().lastPosted().has_value());
    CHECK(f.app.days().view(kMon).entries.empty());
}

TEST_CASE (

"an item you already added by hand that day is not posted again"
)
 {
    Fixture f;
    f.app.entries().addFromItem(kMon, f.matatu, 70);
    const auto r = f.app.dailyJob().run(kMon, kEvening);
    CHECK(r.entries == 0);
    const auto day = f.app.days().view(kMon);
    REQUIRE(day.entries.size() == 1);
    CHECK(day.entries[0].kind == EntryKind::Added);
    CHECK(day.entries[0].amount == 70);
    CHECK(day.calculated);
}

TEST_CASE (

"a one-off added before posting does not stop the routine"
)
 {
    Fixture f;
    f.app.entries().addOneOff(kMon, "Uncle Bill's wedding", 500);
    CHECK(f.app.dailyJob().run(kMon, kEvening).entries == 1);
    CHECK(f.app.days().view(kMon).entries.size() == 2);
}

TEST_CASE (

"paused rules, items and categories do not post"
)
 {
    {
        Fixture f;
        f.app.rules().setActive(f.rule, false);
        CHECK(f.app.dailyJob().run(kMon, kEvening).entries == 0);
    }
    {
        Fixture f;
        f.app.items().setActive(f.matatu, false);
        CHECK(f.app.dailyJob().run(kMon, kEvening).entries == 0);
    }
    {
        Fixture f;
        f.app.categories().setActive(f.app.items().find(f.matatu)->categoryId, false);
        CHECK(f.app.dailyJob().run(kMon, kEvening).entries == 0);
    }
}

TEST_CASE (

"an item in two rules posts once a day"
)
 {
    Fixture f;
    f.app.rules().add({.name = "Also Mondays", .schedule = Schedule::weekly(WeekdaySet{pesa::Weekday::Mon}),
                       .effectiveFrom = Date::parse("2026-11-01"), .effectiveTo = std::nullopt,
                       .itemIds = {f.matatu}});
    CHECK(f.app.dailyJob().run(kMon, kEvening).entries == 1);
}