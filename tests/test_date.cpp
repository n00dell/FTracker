// FTracker/tests/test_date.cpp
#include <doctest/doctest.h>

#include <pesa/date.h>

using pesa::Date;
using pesa::Error;
using pesa::Weekday;

TEST_CASE("a date round-trips through text") {
    const Date d = Date::parse("2026-11-23");
    CHECK(d.toString() == "2026-11-23");
    CHECK(d.year() == 2026);
    CHECK(d.month() == 11);
    CHECK(d.day() == 23);
    CHECK(Date::from(2026, 1, 5).toString() == "2026-01-05");   // zero padded
}

TEST_CASE("dates that do not exist are refused") {
    for (const char* bad : {"", "2026-11-3", "2026-1-03", "26-11-03", "2026/11/03", "2026-11-03 ",
                            "abcd-ef-gh", "2026-02-30", "2026-13-01", "2026-00-10", "2026-11-00",
                            "2027-02-29", "2100-02-29", "1800-01-01", "2026-+1-03"}) {
        CHECK_THROWS_AS((void)Date::parse(bad), Error);
    }
    CHECK_THROWS_AS((void)Date::from(2026, 4, 31), Error);
}

TEST_CASE("leap years are understood") {
    CHECK_NOTHROW((void)Date::parse("2028-02-29"));
    CHECK_NOTHROW((void)Date::parse("2000-02-29"));
    CHECK(Date::parse("2028-02-28").nextDay().toString() == "2028-02-29");
    CHECK(Date::parse("2027-02-28").nextDay().toString() == "2027-03-01");
}

TEST_CASE("adding days crosses month and year ends") {
    CHECK(Date::parse("2026-11-30").nextDay().toString() == "2026-12-01");
    CHECK(Date::parse("2026-12-31").nextDay().toString() == "2027-01-01");
    CHECK(Date::parse("2027-01-01").previousDay().toString() == "2026-12-31");
    CHECK(Date::parse("2026-11-23").addDays(10).toString() == "2026-12-03");
    CHECK(Date::parse("2026-11-23").addDays(-23).toString() == "2026-10-31");
    CHECK(Date::parse("2026-11-23").addDays(0) == Date::parse("2026-11-23"));
}

TEST_CASE("the weekday is right") {
    CHECK(Date::parse("2026-11-23").weekday() == Weekday::Mon);   // a Monday
    CHECK(Date::parse("2026-11-27").weekday() == Weekday::Fri);
    CHECK(Date::parse("2026-11-28").weekday() == Weekday::Sat);
    CHECK(Date::parse("2026-11-29").weekday() == Weekday::Sun);
    CHECK(Date{}.weekday() == Weekday::Thu);                       // 1970-01-01
}

TEST_CASE("dates compare and count days between them") {
    const Date a = Date::parse("2026-11-23");
    const Date b = Date::parse("2026-11-28");
    CHECK(a < b);
    CHECK(b > a);
    CHECK(a == Date::parse("2026-11-23"));
    CHECK(a != b);
    CHECK(a.daysUntil(b) == 5);
    CHECK(b.daysUntil(a) == -5);
    CHECK(a.daysUntil(a) == 0);
    CHECK(Date::parse("2026-12-31").daysUntil(Date::parse("2027-01-01")) == 1);
    CHECK(Date{}.toString() == "1970-01-01");
}
