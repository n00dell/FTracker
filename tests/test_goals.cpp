//
// Created by lee on 11/26/26.
//
// FTracker/tests/test_goals.cpp
#include <doctest/doctest.h>

#include <pesa/pesa.h>

using pesa::BuyMethod;
using pesa::Date;
using pesa::Error;
using pesa::FundedFrom;
using pesa::IncomeMode;
using pesa::Pesa;

namespace {
    const Date kToday = Date::parse("2026-11-24");

    // An irregular income with 10,000 earned, and Main savings at 0.
    struct Fixture {
        Pesa app{":memory:"};
        Fixture() { app.completeSetup({.mode = IncomeMode::Irregular, .startingBalance = 10000}, kToday); }

        pesa::GoalId goal(const std::string &name, pesa::Money amount, int priority,
                          FundedFrom from = FundedFrom::Savings) {
            return app.goals().add({.name = name, .amount = amount, .priority = priority, .fundedFrom = from});
        }
    };
} // namespace

TEST_CASE("goals are listed by priority, lowest number first") {
    Fixture f;
    f.goal("Desk", 12000, 40);
    f.goal("Guitar", 30000, 1);
    f.goal("Curtains", 6500, 20);

    const auto open = f.app.goals().list();
    REQUIRE(open.size() == 3);
    CHECK(open[0].name == "Guitar");
    CHECK(open[1].name == "Curtains");
    CHECK(open[2].name == "Desk");
    CHECK_FALSE(open[0].isBought());
}

TEST_CASE("bad goals are refused") {
    Fixture f;
    f.goal("Guitar", 30000, 1);
    CHECK_THROWS_AS(f.goal("  ", 100, 1), Error);
    CHECK_THROWS_AS(f.goal("Free", 0, 1), Error);
    CHECK_THROWS_AS(f.goal("Late", 100, 0), Error);
    CHECK_THROWS_AS(f.goal("guitar", 100, 2), Error); // names are unique
    CHECK_THROWS_AS(f.app.goals().add({.name = "Auto", .amount = 100, .priority = 1, .autoBuy = true}), Error);
    // no date
    CHECK(f.app.goals().list().size() == 1);
}

TEST_CASE("covered or short is counted in priority order, per place the money comes from") {
    Fixture f;
    f.app.savings().add(kToday, f.app.savings().mainPot(), 35000, "Gift");
    f.goal("Guitar", 30000, 1);
    f.goal("Curtains", 6500, 20);
    f.goal("Desk", 12000, 40, FundedFrom::Earned);
    f.app.goals().add({.name = "Phone case", .amount = 1500, .priority = 5, .donated = true});

    const auto status = f.app.goals().statuses(kToday);
    REQUIRE(status.size() == 4);
    CHECK(status[0].goal.name == "Guitar");
    CHECK(status[0].covered); // 30,000 of 35,000
    CHECK(status[1].goal.name == "Phone case");
    CHECK(status[1].covered); // a gift takes nothing
    CHECK(status[2].goal.name == "Curtains");
    CHECK_FALSE(status[2].covered);
    CHECK(status[2].shortBy == 1500); // 36,500 needed for it and the guitar, 35,000 in savings
    CHECK(status[3].goal.name == "Desk");
    CHECK(status[3].shortBy == 2000); // 12,000 against 10,000 earned
}

TEST_CASE("goals can be edited, but not once bought") {
    Fixture f;
    const auto id = f.goal("Desk", 12000, 40);
    f.app.goals().edit(id, {
                           .amount = 13000, .priority = 30, .dueDate = Date::parse("2026-12-20"),
                           .fundedFrom = FundedFrom::Earned, .autoBuy = true
                       });
    const auto g = f.app.goals().find(id);
    REQUIRE(g.has_value());
    CHECK(g->amount == 13000);
    CHECK(g->priority == 30);
    CHECK(g->dueDate == Date::parse("2026-12-20"));
    CHECK(g->fundedFrom == FundedFrom::Earned);
    CHECK(g->autoBuy);

    CHECK_THROWS_AS(f.app.goals().edit(id, {.clearDate = true}), Error); // auto-buy needs its date
    f.app.goals().edit(id, {.clearDate = true, .autoBuy = false});
    CHECK_FALSE(f.app.goals().find(id)->dueDate.has_value());
    CHECK_THROWS_AS(f.app.goals().edit(id, {.amount = 0}), Error);
    CHECK_THROWS_AS(f.app.goals().edit(9999, {.amount = 5}), Error);

    f.app.goals().buy(id, BuyMethod::FromEarned, kToday);
    CHECK_THROWS_AS(f.app.goals().edit(id, {.amount = 5}), Error);
}

TEST_CASE("buying from savings takes the price out of the pot") {
    Fixture f;
    f.app.savings().add(kToday, f.app.savings().mainPot(), 35000, "Gift");
    const auto id = f.goal("Guitar", 30000, 1);

    f.app.goals().buy(id, BuyMethod::FromSavings, kToday);
    CHECK(f.app.savings().total() == 5000);
    CHECK(f.app.goals().list().empty());
    const auto bought = f.app.goals().list(true);
    REQUIRE(bought.size() == 1);
    CHECK(bought[0].boughtOn == kToday);
    CHECK(bought[0].boughtHow == BuyMethod::FromSavings);
    const auto ledger = f.app.savings().ledger();
    CHECK(ledger.back().kind == pesa::LedgerKind::GoalPurchase);
    CHECK(ledger.back().amount == -30000);
    CHECK(f.app.periods().summary(kToday).earnedRemaining() == 10000); // earned is untouched

    CHECK_THROWS_AS(f.app.goals().buy(id, BuyMethod::FromSavings, kToday), Error); // already bought
    CHECK(f.app.savings().total() == 5000);
}

TEST_CASE("moving savings to earned and buying leaves earned where it was") {
    Fixture f;
    f.app.savings().add(kToday, f.app.savings().mainPot(), 35000, "Gift");
    const auto id = f.goal("Desk", 12000, 40, FundedFrom::Earned);

    f.app.goals().buy(id, BuyMethod::MoveToEarned, kToday);
    CHECK(f.app.savings().total() == 23000);
    const auto s = f.app.periods().summary(kToday);
    CHECK(s.income == 22000);
    CHECK(s.spentFromEarned == 12000);
    CHECK(s.earnedRemaining() == 10000);
    const auto day = f.app.days().view(kToday);
    REQUIRE(day.entries.size() == 1);
    CHECK(day.entries[0].name == "Desk");
    CHECK(day.entries[0].isOneOff());
}

TEST_CASE("buying from earned is just an expense, and a gift deducts nothing") {
    Fixture f;
    const auto desk = f.goal("Desk", 12000, 40, FundedFrom::Earned);
    f.app.goals().buy(desk, BuyMethod::FromEarned, kToday);
    CHECK(f.app.periods().summary(kToday).earnedRemaining() == -2000); // allowed: earned can go negative
    CHECK(f.app.savings().total() == 0);

    const auto gift = f.app.goals().add({.name = "Wedding gift", .amount = 5000, .priority = 2, .donated = true});
    f.app.goals().buy(gift, BuyMethod::FromSavings, kToday);
    CHECK(f.app.savings().total() == 0);
    CHECK(f.app.days().view(kToday).entries.size() == 1); // only the desk
    const auto bought = f.app.goals().list(true);
    REQUIRE(bought.size() == 2);
    CHECK(bought[0].donated);
    CHECK_FALSE(bought[0].boughtHow.has_value());
}

TEST_CASE("a purchase that fails leaves the goal open and the money untouched") {
    Pesa app(":memory:"); // no period yet
    app.savings().add(kToday, app.savings().mainPot(), 5000, "Gift");
    const auto id = app.goals().add({.name = "Desk", .amount = 2000, .priority = 1});
    CHECK_THROWS_AS(app.goals().buy(id, BuyMethod::MoveToEarned, kToday), Error); // earned needs an open period
    CHECK_FALSE(app.goals().find(id)->isBought());
    CHECK(app.savings().total() == 5000);
    CHECK_THROWS_AS(app.goals().buy(id, BuyMethod::FromSavings, kToday, 9999), Error); // no such pot
    CHECK_FALSE(app.goals().find(id)->isBought());
}

TEST_CASE("a savings purchase can come out of another pot") {
    Fixture f;
    const auto sacco = f.app.savings().addPot("Sacco");
    f.app.savings().add(kToday, sacco, 9000, "Contribution");
    const auto id = f.goal("Curtains", 6500, 20);
    f.app.goals().buy(id, BuyMethod::FromSavings, kToday, sacco);
    CHECK(f.app.savings().findPot(sacco)->balance == 2500);
    CHECK(f.app.savings().findPot(f.app.savings().mainPot())->balance == 0);
}

TEST_CASE("goals that reached their date: some buy themselves, the rest wait for you") {
    Fixture f;
    f.app.savings().add(kToday, f.app.savings().mainPot(), 50000, "Gift");
    f.app.goals().add({
        .name = "Guitar", .amount = 30000, .priority = 1, .dueDate = Date::parse("2026-11-10"),
        .fundedFrom = FundedFrom::Savings, .autoBuy = true
    });
    f.app.goals().add({
        .name = "Desk", .amount = 12000, .priority = 2, .dueDate = Date::parse("2026-11-12"),
        .fundedFrom = FundedFrom::Earned, .autoBuy = true
    });
    f.app.goals().add({
        .name = "Curtains", .amount = 6500, .priority = 3, .dueDate = Date::parse("2026-11-20"),
        .fundedFrom = FundedFrom::Savings
    });
    f.app.goals().add({
        .name = "Soundbar", .amount = 45000, .priority = 4, .dueDate = Date::parse("2026-12-30"),
        .fundedFrom = FundedFrom::Savings, .autoBuy = true
    }); // not due yet

    const auto due = f.app.goals().processDue(kToday);
    REQUIRE(due.bought.size() == 2);
    CHECK(due.bought[0].name == "Guitar");
    REQUIRE(due.waiting.size() == 1);
    CHECK(due.waiting[0].name == "Curtains");
    CHECK(due.problems.empty());

    CHECK(f.app.savings().ledger().back().date == Date::parse("2026-11-10")); // bought on its own date
    const auto desk = f.app.days().view(kToday).entries; // before this period began: clamped to its start
    REQUIRE(desk.size() == 1);
    CHECK(desk[0].name == "Desk");
    CHECK(f.app.goals().list().size() == 2); // Curtains and Soundbar remain
    CHECK(f.app.goals().processDue(kToday).bought.empty()); // nothing is bought twice
}

TEST_CASE("deleting a goal frees its name") {
    Fixture f;
    const auto id = f.goal("Desk", 12000, 40);
    f.app.goals().remove(id);
    CHECK(f.app.goals().list().empty());
    CHECK_FALSE(f.app.goals().find(id).has_value());
    CHECK_THROWS_AS(f.app.goals().remove(id), Error);
    CHECK_NOTHROW(f.goal("Desk", 12000, 40));
}

TEST_CASE("a fund reads the balance of its pot") {
    Fixture f;
    const auto lock = f.app.savings().addPot("Lock savings", pesa::PotKind::Locked);
    const auto id = f.app.goals().addFund("Emergency fund", 200000, lock);
    f.app.savings().add(kToday, lock, 50000, "Start");

    auto funds = f.app.goals().funds();
    REQUIRE(funds.size() == 1);
    CHECK(funds[0].potName == "Lock savings");
    CHECK(funds[0].balance == 50000);
    CHECK(funds[0].target == 200000);
    f.app.savings().add(kToday, lock, 25000, "More");
    CHECK(f.app.goals().funds()[0].balance == 75000); // nothing is stored twice

    CHECK_THROWS_AS(f.app.goals().addFund("Emergency fund", 1000, lock), Error);
    CHECK_THROWS_AS(f.app.goals().addFund("Zero", 0, lock), Error);
    CHECK_THROWS_AS(f.app.goals().addFund("No pot", 1000, 9999), Error);
    f.app.goals().removeFund(id);
    CHECK(f.app.goals().funds().empty());
    CHECK_THROWS_AS(f.app.goals().removeFund(id), Error);
}
