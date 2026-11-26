//
// Created by lee on 11/26/26.
//
// FTracker/tests/test_corrections.cpp
#include <doctest/doctest.h>

#include <pesa/pesa.h>

using pesa::Date;
using pesa::EntryKind;
using pesa::Error;
using pesa::LogKind;
using pesa::Mode;
using pesa::Pesa;
using pesa::RuleKind;
using pesa::Schedule;
using pesa::TimeOfDay;
using pesa::WeekdaySet;

namespace {
    const TimeOfDay kEvening{21, 0};
    const Date kToday = Date::parse("2026-11-24");

    Date day(int d) { return Date::from(2026, 11, d); }

    // Matatu (50, Transport) and Tea (60, Food), both posted every day since 1 November.
    struct Fixture {
        Pesa app{":memory:"};
        pesa::CategoryId transport = 0;
        pesa::ItemId matatu = 0;
        pesa::ItemId tea = 0;

        Fixture() {
            transport = app.categories().add("Transport");
            const auto food = app.categories().add("Food");
            matatu = app.items().add({.categoryId = transport, .name = "Matatu", .defaultAmount = 50});
            tea = app.items().add({.categoryId = food, .name = "Tea", .defaultAmount = 60});
            app.rules().add({
                .name = "Daily", .schedule = Schedule::weekly(WeekdaySet::everyDay()),
                .effectiveFrom = Date::parse("2026-11-01"), .effectiveTo = std::nullopt,
                .itemIds = {matatu, tea}
            });
        }

        // Runs the job once per evening, as if Pesa had been opened every day. (The first run posts only its own day.)
        void post(int from, int to) {
            for (int d = from; d <= to; ++d) app.dailyJob().run(day(d), kEvening);
        }

        [[nodiscard]] pesa::NewRule teaRise(const Date &from, std::optional<Date> to, pesa::Money extra = 10) const {
            return pesa::NewRule{
                .name = "Tea price rise", .effectiveFrom = from, .effectiveTo = to,
                .itemIds = {tea}, .kind = RuleKind::Adjust, .extra = extra
            };
        }
    };

    pesa::Entry entryOn(Pesa &app, const Date &date, const std::string &name) {
        for (const auto &e: app.days().view(date).entries) {
            if (e.name == name) return e;
        }
        FAIL("no entry called " << name << " on " << date.toString());
        return {};
    }

    bool has(const std::vector<pesa::LogEvent> &events, LogKind kind, const std::string &part) {
        for (const auto &e: events) {
            if (e.kind == kind && e.message.find(part) != std::string::npos) return true;
        }
        return false;
    }
} // namespace

// ---------------- adjust rules ----------------

TEST_CASE("an adjust rule from the past is previewed first, then recalculates posted days") {
    Fixture f;
    f.post(20, 24);

    const auto preview = f.app.rules().addAdjustment(f.teaRise(day(22), std::nullopt), kToday, Mode::Preview);
    CHECK(preview.changes.entries == 3); // Tea on the 22nd, 23rd, 24th
    CHECK(preview.changes.days == 3);
    CHECK(preview.changes.before == 180);
    CHECK(preview.changes.after == 210);
    REQUIRE(preview.changes.sample.size() == 3);
    CHECK(preview.changes.sample[0].after == 70);
    CHECK_FALSE(preview.restorePoint.has_value());
    // A preview leaves no trace at all.
    CHECK(entryOn(f.app, day(22), "Tea").amount == 60);
    CHECK(f.app.rules().list().size() == 1);
    CHECK(f.app.restorePoints().list().empty());
    CHECK(f.app.log().list(pesa::LogFilter::Edits).empty());

    const auto done = f.app.rules().addAdjustment(f.teaRise(day(22), std::nullopt), kToday);
    CHECK(done.changes.entries == 3);
    REQUIRE(done.restorePoint.has_value());
    CHECK(done.created != 0);
    CHECK(entryOn(f.app, day(21), "Tea").amount == 60); // before the rule starts
    CHECK(entryOn(f.app, day(22), "Tea").amount == 70);
    CHECK(f.app.days().view(day(22)).total() == 120);
    CHECK(f.app.days().savedResult(day(22)) == 120);
    CHECK(has(f.app.log().list(), LogKind::Edit, "Rule 'Tea price rise' (+10) changed 3 posted entries"));
}

TEST_CASE("entries you edited by hand are not touched by a bulk adjustment") {
    Fixture f;
    f.post(20, 24);
    f.app.entries().setAmount(entryOn(f.app, day(23), "Tea").id, 65);

    const auto done = f.app.rules().addAdjustment(f.teaRise(day(22), std::nullopt), kToday);
    CHECK(done.changes.entries == 2);
    CHECK(done.changes.keptEdited == 1);
    CHECK(entryOn(f.app, day(23), "Tea").amount == 65);
    CHECK(entryOn(f.app, day(23), "Tea").kind == EntryKind::Edited);
    CHECK(entryOn(f.app, day(24), "Tea").amount == 70);
}

TEST_CASE("an adjustment keeps savings in step for entries paid from savings") {
    Fixture f;
    f.post(20, 24);
    const std::vector<pesa::EntryId> ids{entryOn(f.app, day(22), "Tea").id};
    f.app.entries().coverFromSavings(ids);
    CHECK(f.app.savings().total() == -60);

    f.app.rules().addAdjustment(f.teaRise(day(22), std::nullopt), kToday);
    CHECK(f.app.savings().total() == -70);
}

TEST_CASE("an adjust rule that starts later changes nothing now and applies when posting") {
    Fixture f;
    f.post(20, 24);
    const auto r = f.app.rules().addAdjustment(f.teaRise(day(26), day(27)), kToday);
    CHECK(r.changes.entries == 0);
    CHECK_FALSE(r.restorePoint.has_value());

    f.post(25, 28);
    CHECK(entryOn(f.app, day(25), "Tea").amount == 60);
    CHECK(entryOn(f.app, day(26), "Tea").amount == 70);
    CHECK(entryOn(f.app, day(27), "Tea").amount == 70);
    CHECK(entryOn(f.app, day(28), "Tea").amount == 60); // the rule has ended
}

TEST_CASE("bad adjust rules are refused and leave nothing behind") {
    Fixture f;
    f.post(20, 24);
    CHECK_THROWS_AS(f.app.rules().addAdjustment(f.teaRise(day(22), std::nullopt, 0), kToday), Error);
    CHECK_THROWS_AS(f.app.rules().addAdjustment(f.teaRise(day(22), std::nullopt, -60), kToday), Error); // 60 - 60 = 0

    auto noItems = f.teaRise(day(22), std::nullopt);
    noItems.itemIds.clear();
    CHECK_THROWS_AS(f.app.rules().addAdjustment(noItems, kToday), Error);

    auto charge = f.teaRise(day(22), std::nullopt);
    charge.kind = RuleKind::Charge;
    CHECK_THROWS_AS(f.app.rules().addAdjustment(charge, kToday), Error);
    CHECK_THROWS_AS(f.app.rules().add(f.teaRise(day(22), std::nullopt)), Error); // add() is for charge rules

    CHECK(f.app.rules().list().size() == 1);
    CHECK(f.app.restorePoints().list().empty());
}

// ---------------- skips ----------------

TEST_CASE("a skip over posted days greys them out, refunds savings, and can be previewed") {
    Fixture f;
    f.post(20, 24);
    const std::vector<pesa::EntryId> ids{entryOn(f.app, day(22), "Matatu").id};
    f.app.entries().coverFromSavings(ids);
    CHECK(f.app.savings().total() == -50);

    const pesa::NewSkip holiday{.name = "Holiday", .from = day(22), .to = day(23), .categories = {f.transport}};
    const auto preview = f.app.skips().add(holiday, kToday, Mode::Preview);
    CHECK(preview.changes.entries == 2);
    CHECK(preview.changes.days == 2);
    CHECK(preview.changes.before == 100);
    CHECK(preview.changes.after == 0);
    CHECK(f.app.skips().list().empty()); // nothing was saved
    CHECK(entryOn(f.app, day(22), "Matatu").kind == EntryKind::AutoPosted);

    const auto done = f.app.skips().add(holiday, kToday);
    REQUIRE(done.restorePoint.has_value());
    const auto grey = entryOn(f.app, day(22), "Matatu");
    CHECK(grey.kind == EntryKind::Skipped);
    CHECK(grey.skipName == "Holiday");
    CHECK(grey.amount == 50); // what it would have been
    CHECK(f.app.days().view(day(22)).total() == 60); // only Tea counts
    CHECK(f.app.savings().total() == 0); // the cover was refunded
    CHECK(entryOn(f.app, day(22), "Tea").kind == EntryKind::AutoPosted); // other categories are untouched
    CHECK(has(f.app.log().list(pesa::LogFilter::Skipped), LogKind::Skip, "marked 2 posted entries as skipped"));
}

TEST_CASE("a skip leaves entries you edited or added by hand alone") {
    Fixture f;
    f.post(20, 24);
    f.app.entries().setAmount(entryOn(f.app, day(23), "Matatu").id, 55); // edited
    f.app.entries().addFromItem(day(23), f.matatu, 40); // added by hand

    const pesa::NewSkip holiday{.name = "Holiday", .from = day(22), .to = day(23), .items = {f.matatu}};
    const auto done = f.app.skips().add(holiday, kToday);
    CHECK(done.changes.entries == 1); // only the untouched one, on the 22nd
    CHECK(done.changes.keptEdited == 1);
    CHECK(done.changes.keptAdded == 1);
    CHECK(f.app.days().view(day(23)).total() == 55 + 40 + 60);
}

TEST_CASE("a skip in the future makes the daily job write grey rows") {
    Fixture f;
    f.post(20, 24);
    const pesa::NewSkip trip{.name = "Trip", .from = day(26), .to = day(27), .items = {f.tea}};
    const auto r = f.app.skips().add(trip, kToday);
    CHECK(r.changes.entries == 0);
    CHECK_FALSE(r.restorePoint.has_value()); // the past was not touched

    f.app.dailyJob().run(day(25), kEvening);
    const auto onTheDay = f.app.dailyJob().run(day(26), kEvening);
    CHECK(onTheDay.skipped == 1);
    CHECK(onTheDay.entries == 1);
    const auto grey = entryOn(f.app, day(26), "Tea");
    CHECK(grey.kind == EntryKind::Skipped);
    CHECK(grey.skipName == "Trip");
    CHECK(grey.amount == 60);
    CHECK(entryOn(f.app, day(26), "Matatu").kind == EntryKind::AutoPosted);
    CHECK(f.app.days().view(day(26)).total() == 50);
    CHECK(has(f.app.log().list(), LogKind::Skip, "Skipped Tea (Trip) on 2026-11-26"));

    f.post(27, 28);
    CHECK(entryOn(f.app, day(27), "Tea").kind == EntryKind::Skipped);
    CHECK(entryOn(f.app, day(28), "Tea").kind == EntryKind::AutoPosted);
}

TEST_CASE("removing a skip brings its days back") {
    Fixture f;
    f.post(20, 24);
    const pesa::NewSkip holiday{.name = "Holiday", .from = day(22), .to = day(23), .categories = {f.transport}};
    const auto added = f.app.skips().add(holiday, kToday);

    const auto preview = f.app.skips().remove(added.created, Mode::Preview);
    CHECK(preview.changes.entries == 2);
    CHECK(preview.changes.after == 100);
    CHECK(entryOn(f.app, day(22), "Matatu").kind == EntryKind::Skipped); // a preview changes nothing

    const auto done = f.app.skips().remove(added.created);
    CHECK(done.changes.entries == 2);
    const auto back = entryOn(f.app, day(22), "Matatu");
    CHECK(back.kind == EntryKind::AutoPosted);
    CHECK(back.skipName.empty());
    CHECK(f.app.days().view(day(22)).total() == 110);
    CHECK(f.app.skips().list().empty());
    CHECK(f.app.skips().list(true).size() == 1);
    CHECK_THROWS_AS(f.app.skips().remove(added.created), Error); // already removed
}

TEST_CASE("an entry that another skip still covers stays grey when one skip is removed") {
    Fixture f;
    f.post(20, 24);
    const pesa::NewSkip holiday{.name = "Holiday", .from = day(22), .to = day(23), .categories = {f.transport}};
    const auto first = f.app.skips().add(holiday, kToday);
    const pesa::NewSkip other{.name = "Other", .from = day(23), .to = day(23), .items = {f.matatu}};
    f.app.skips().add(other, kToday);

    f.app.skips().remove(first.created);
    CHECK(entryOn(f.app, day(22), "Matatu").kind == EntryKind::AutoPosted);
    const auto stillGrey = entryOn(f.app, day(23), "Matatu");
    CHECK(stillGrey.kind == EntryKind::Skipped);
    CHECK(stillGrey.skipName == "Other");
}

TEST_CASE("bad skips are refused") {
    Fixture f;
    CHECK_THROWS_AS(f.app.skips().add({.name = "X", .from = day(22), .to = day(21), .items = {f.tea}}, kToday), Error);
    CHECK_THROWS_AS(f.app.skips().add({.name = "X", .from = day(22), .to = day(22)}, kToday), Error); // nothing to skip
    CHECK_THROWS_AS(f.app.skips().add({.name = " ", .from = day(22), .to = day(22), .items = {f.tea}}, kToday), Error);
    CHECK_THROWS_AS(f.app.skips().add({.name = "X", .from = day(22), .to = day(22), .items = {9999}}, kToday), Error);
    CHECK_THROWS_AS(f.app.skips().add({.name = "X", .from = day(22), .to = day(22), .categories = {9999}}, kToday),
                    Error);
    CHECK(f.app.skips().list(true).empty());
}

// ---------------- wipe all ----------------

TEST_CASE("wipe all removes only untouched rule-posted entries, and the job does not repost them") {
    Fixture f;
    f.post(20, 24);
    f.app.entries().setAmount(entryOn(f.app, day(21), "Tea").id, 65); // edited
    f.app.entries().addOneOff(day(22), "Gift", 500); // added
    const std::vector<pesa::EntryId> ids{entryOn(f.app, day(21), "Matatu").id};
    f.app.entries().coverFromSavings(ids);
    CHECK(f.app.savings().total() == -50);

    const auto preview = f.app.days().wipe(day(21), day(22), kToday, Mode::Preview);
    CHECK(preview.changes.entries == 3); // Matatu 21st, Matatu + Tea 22nd
    CHECK(preview.changes.days == 2);
    CHECK(preview.changes.before == 160);
    CHECK(preview.changes.keptEdited == 1);
    CHECK(preview.changes.keptAdded == 1);
    CHECK(f.app.days().view(day(22)).entries.size() == 3); // nothing happened yet

    const auto done = f.app.days().wipe(day(21), day(22), kToday);
    CHECK(done.changes.entries == 3);
    REQUIRE(done.restorePoint.has_value());
    CHECK(f.app.days().view(day(21)).total() == 65); // the edited Tea stays
    CHECK(f.app.days().view(day(22)).total() == 500); // the one-off stays
    CHECK(f.app.savings().total() == 0); // the covered entry was refunded
    CHECK(f.app.days().view(day(22)).calculated);

    CHECK(f.app.dailyJob().run(kToday, kEvening).entries == 0); // wiped days are not posted again
    CHECK(f.app.days().view(day(22)).entries.size() == 1);
}

TEST_CASE("wipe all is for past days only, and says when there is nothing to wipe") {
    Fixture f;
    f.post(20, 24);
    CHECK_THROWS_AS(f.app.days().wipe(day(22), day(21), kToday), Error);
    CHECK_THROWS_AS(f.app.days().wipe(day(24), day(25), kToday), Error); // 25th is in the future
    const auto none = f.app.days().wipe(day(1), day(5), kToday); // days with no entries
    CHECK(none.changes.entries == 0);
    CHECK_FALSE(none.restorePoint.has_value());
}

// ---------------- restore points ----------------

TEST_CASE("going back after a wipe puts the days and the savings back, and can itself be undone") {
    Fixture f;
    f.post(20, 24);
    const std::vector<pesa::EntryId> ids{entryOn(f.app, day(21), "Matatu").id};
    f.app.entries().coverFromSavings(ids);

    const auto wiped = f.app.days().wipe(day(21), day(22), kToday);
    REQUIRE(wiped.restorePoint.has_value());
    CHECK(f.app.days().view(day(21)).entries.empty());
    CHECK(f.app.savings().total() == 0);

    const auto back = f.app.restorePoints().restore(*wiped.restorePoint);
    CHECK(back.entries == 4);
    REQUIRE(back.undoPoint.has_value());
    CHECK(f.app.days().view(day(21)).total() == 110);
    CHECK(f.app.days().view(day(22)).total() == 110);
    CHECK(f.app.days().savedResult(day(21)) == 110);
    CHECK(f.app.savings().total() == -50); // the cover is charged again
    CHECK(has(f.app.log().list(pesa::LogFilter::Edits), LogKind::Edit, "Went back to restore point"));

    f.app.restorePoints().restore(*back.undoPoint); // undo the undo
    CHECK(f.app.days().view(day(21)).entries.empty());
    CHECK(f.app.savings().total() == 0);
}

TEST_CASE("going back after an adjust rule restores the amounts and switches the rule off") {
    Fixture f;
    f.post(20, 24);
    const auto done = f.app.rules().addAdjustment(f.teaRise(day(22), std::nullopt), kToday);
    REQUIRE(done.restorePoint.has_value());

    f.app.restorePoints().restore(*done.restorePoint);
    CHECK(entryOn(f.app, day(22), "Tea").amount == 60);
    CHECK(entryOn(f.app, day(24), "Tea").amount == 60);
    CHECK(f.app.rules().find(done.created)->isDeleted);
    f.post(25, 25);
    CHECK(entryOn(f.app, day(25), "Tea").amount == 60); // the rule no longer applies
}

TEST_CASE("going back after a skip restores the entries and removes the skip") {
    Fixture f;
    f.post(20, 24);
    const pesa::NewSkip holiday{.name = "Holiday", .from = day(22), .to = day(23), .categories = {f.transport}};
    const auto done = f.app.skips().add(holiday, kToday);

    f.app.restorePoints().restore(*done.restorePoint);
    CHECK(entryOn(f.app, day(22), "Matatu").kind == EntryKind::AutoPosted);
    CHECK(f.app.skips().find(done.created)->isDeleted);
    CHECK(f.app.skips().list().empty());
}

TEST_CASE("only the newest five restore points are kept") {
    Fixture f;
    f.post(10, 24);
    pesa::RestorePointId first = 0;
    for (int d = 10; d <= 15; ++d) {
        const auto wiped = f.app.days().wipe(day(d), day(d), kToday);
        if (d == 10) first = *wiped.restorePoint;
    }
    const auto points = f.app.restorePoints().list();
    REQUIRE(points.size() == 5);
    CHECK(points[0].label.find("Before wiping 2026-11-15") != std::string::npos); // newest first
    CHECK(points[0].entries == 2);
    CHECK_THROWS_AS(f.app.restorePoints().restore(first), Error); // the oldest one is gone
    CHECK_THROWS_AS(f.app.restorePoints().restore(9999), Error);
}
