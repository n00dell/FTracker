// FTracker/tests/test_weekdays.cpp
#include <doctest/doctest.h>

#include <pesa/common.h>

using pesa::Error;
using pesa::Weekday;
using pesa::WeekdaySet;

TEST_CASE (

"WeekdaySet add, remove and contains"
)
 {
    WeekdaySet s{Weekday::Mon, Weekday::Fri};
    CHECK(s.contains(Weekday::Mon));
    CHECK(s.contains(Weekday::Fri));
    CHECK_FALSE(s.contains(Weekday::Tue));
    s.add(Weekday::Tue);
    s.remove(Weekday::Mon);
    CHECK(s.contains(Weekday::Tue));
    CHECK_FALSE(s.contains(Weekday::Mon));
    CHECK(WeekdaySet{}.empty());
}

TEST_CASE (

"WeekdaySet presets and bits round-trip"
)
 {
    CHECK(WeekdaySet::everyDay().bits() == 127);
    CHECK(WeekdaySet::workDays() == WeekdaySet{Weekday::Mon, Weekday::Tue, Weekday::Wed, Weekday::Thu, Weekday::Fri});
    CHECK(WeekdaySet::weekend() == WeekdaySet{Weekday::Sat, Weekday::Sun});
    CHECK(WeekdaySet::fromBits(WeekdaySet::weekend().bits()) == WeekdaySet::weekend());
    CHECK(WeekdaySet::fromBits(0xFFFF) == WeekdaySet::everyDay());   // stray bits are dropped
}

TEST_CASE (

"WeekdaySet::toString"
)
 {
    CHECK(WeekdaySet::everyDay().toString() == "Every day");
    CHECK(WeekdaySet::workDays().toString() == "Mon-Fri");
    CHECK(WeekdaySet{}.toString() == "Never");
    CHECK(WeekdaySet{Weekday::Mon, Weekday::Wed}.toString() == "Mon,Wed");
}

TEST_CASE (

"WeekdaySet::parse"
)
 {
    CHECK(WeekdaySet::parse("mon,wed") == WeekdaySet{Weekday::Mon, Weekday::Wed});
    CHECK(WeekdaySet::parse(" Monday , FRI ") == WeekdaySet{Weekday::Mon, Weekday::Fri});
    CHECK(WeekdaySet::parse("daily") == WeekdaySet::everyDay());
    CHECK(WeekdaySet::parse("weekdays") == WeekdaySet::workDays());
    CHECK(WeekdaySet::parse("weekend,mon").contains(Weekday::Mon));
    CHECK(WeekdaySet::parse("none").empty());
    CHECK_THROWS_AS(WeekdaySet::parse("funday"), Error);
    CHECK_THROWS_AS(WeekdaySet::parse(""), Error);
    CHECK_THROWS_AS(WeekdaySet::parse(" , "), Error);
}
