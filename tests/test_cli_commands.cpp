// FTracker/tests/test_cli_commands.cpp
#include <doctest/doctest.h>

#include <cli/commands.h>

#include <sstream>

#include "cli/display.h"

namespace {
    // A pretend terminal: `input` is what the person types, output is collected.
    struct Session {
        pesa::Pesa app{":memory:"};
        std::istringstream in;
        std::ostringstream out;
        std::ostringstream err;
        cli::Console io;
        cli::Context ctx;

        explicit Session(const std::string &input = "") : in(input), io(in, out, err, false), ctx{app, io} {
        }

        int run(const std::string &line) { return cli::runGuarded(ctx, cli::splitLine(line)); }
        [[nodiscard]] std::string output() const { return out.str(); }
        [[nodiscard]] std::string errors() const { return err.str(); }
    };

    bool contains(const std::string &text, const std::string &part) { return text.find(part) != std::string::npos; }

    // Misc is always there, so tests about "my categories" skip it.
    std::vector<pesa::Category> userCategories(const pesa::Pesa &app) {
        std::vector<pesa::Category> out;
        for (auto &c: app.categories().list()) {
            if (!c.isSystem) out.push_back(std::move(c));
        }
        return out;
    }

    const pesa::Date kDay = pesa::Date::parse("2026-11-23");
} // namespace

TEST_CASE(
    "one-liners still work, and a one-off lands in Misc"
) {
    Session s;
    CHECK(s.run("category add Food") == 0);
    CHECK(s.run("item add Food 60 Milk") == 0);
    CHECK(s.run("item add food 55 \"Fresh bread\"") == 0);
    CHECK(s.run("entry add 2026-11-23 1") == 0);
    CHECK(s.run("entry oneoff 2026-11-23 500 Uncle Bill's wedding --savings") == 0);

    const auto day = s.app.days().view(kDay);
    CHECK(day.earnedTotal == 60);
    CHECK(day.savingsTotal == 500);
    REQUIRE(day.entries.size() == 2);
    CHECK(s.app.items().list()[1].name == "Fresh bread");
    CHECK(contains(s.output(), "Uncle Bill's wedding")); // the day is shown after each change
    CHECK(contains(s.output(), "Spent 560"));
    CHECK(day.entries[1].isOneOff());
    CHECK(day.entries[1].categoryName.empty());
}

TEST_CASE(
    "errors go to the error stream and give a failing exit code"
) {
    Session s;
    s.run("category add Food");
    CHECK(s.run("item add Food 0 Bread") == 1);
    CHECK(contains(s.errors(), "more than 0"));
    CHECK(s.run("item add Nowhere 10 X") == 1);
    CHECK(contains(s.errors(), "No category named 'Nowhere'"));
    CHECK(s.run("bogus") == 2);
    CHECK(contains(s.errors(), "Unknown command"));
    CHECK(s.run("day 2026-02-30") == 1);
}

TEST_CASE(
    "Misc cannot be renamed or deleted from the command line"
) {
    Session s;
    const auto misc = std::to_string(s.app.categories().misc());
    CHECK(s.run("category rename " + misc + " Other") == 1);
    CHECK(contains(s.errors(), "built in"));
    CHECK(s.run("category delete " + misc) == 1);
}

TEST_CASE(
    "category add asks for the name"
) {
    Session s("Food\n");
    CHECK(s.run("category add") == 0);
    REQUIRE(userCategories(s.app).size() == 1);
    CHECK(userCategories(s.app)[0].name == "Food");
    CHECK(contains(s.output(), "Category name: "));
}

TEST_CASE(
    "a blank answer or the end of input cancels"
) {
    Session blank("\n");
    CHECK(blank.run("category add") == 1);
    CHECK(contains(blank.output(), "Cancelled."));
    CHECK(userCategories(blank.app).empty());

    Session ended("");
    CHECK(ended.run("category add") == 1);
    CHECK(userCategories(ended.app).empty());

    Session quit("q\n");
    quit.app.categories().add("Food");
    CHECK(quit.run("category rename") == 1); // "q" at a numbered list backs out
}

TEST_CASE(
    "item add walks through category, name and amount"
) {
    Session s("1\nMilk\n60\n");
    s.app.categories().add("Food");
    CHECK(s.run("item add") == 0);

    const auto items = s.app.items().list();
    REQUIRE(items.size() == 1);
    CHECK(items[0].name == "Milk");
    CHECK(items[0].defaultAmount == 60);
    CHECK(contains(s.output(), "1) Food"));
}

TEST_CASE(
    "wrong answers are explained and asked again"
) {
    Session s("1\nMilk\nabc\n0\n60\n"); // bad amount, zero, then 60
    s.app.categories().add("Food");
    CHECK(s.run("item add") == 0);
    CHECK(contains(s.output(), "whole number"));
    CHECK(contains(s.output(), "more than 0"));
    CHECK(s.app.items().list()[0].defaultAmount == 60);
}

TEST_CASE(
    "entry add picks the day, category and item, and Enter takes the defaults"
) {
    Session s("2026-11-23\n1\n1\n\n\n"); // date, category 1, item 1, default amount, default 'earned'
    const auto food = s.app.categories().add("Food");
    s.app.items().add({.categoryId = food, .name = "Milk", .defaultAmount = 60});
    CHECK(s.run("entry add") == 0);

    const auto day = s.app.days().view(kDay);
    REQUIRE(day.entries.size() == 1);
    CHECK(day.entries[0].amount == 60);
    CHECK(day.entries[0].fund == pesa::FundedFrom::Earned);
    CHECK(contains(s.output(), "Spent 60"));
}

TEST_CASE(
    "entry oneoff asks for the day, name, amount and where the money comes from"
) {
    Session s("2026-11-23\nUncle Bill's wedding\n1,500\n2\n"); // date, name, amount, savings
    s.app.categories().add("Food");
    CHECK(s.run("entry oneoff") == 0);

    const auto day = s.app.days().view(kDay);
    REQUIRE(day.entries.size() == 1);
    CHECK(day.entries[0].isOneOff());
    CHECK(day.entries[0].amount == 1500);
    CHECK(day.entries[0].fund == pesa::FundedFrom::Savings);
    CHECK(s.app.items().list().empty()); // never touches the catalog
    CHECK(contains(s.output(), "One-off"));
}

TEST_CASE(
    "deleting an entry in guided mode asks first"
) {
    Session no("2026-11-23\n1\nn\n");
    no.app.entries().addOneOff(kDay, "Gift", 100);
    CHECK(no.run("entry delete") == 0);
    CHECK(no.app.days().view(kDay).entries.size() == 1); // said no

    Session yes("2026-11-23\n1\ny\n");
    yes.app.entries().addOneOff(kDay, "Gift", 100);
    CHECK(yes.run("entry delete") == 0);
    CHECK(yes.app.days().view(kDay).entries.empty());
}

TEST_CASE(
    "pausing offers only categories that can be paused"
) {
    Session s("1\n"); // Food is the only one: Home is paused, Misc is built in
    s.app.categories().add("Food");
    const auto home = s.app.categories().add("Home");
    s.app.categories().setActive(home, false);
    CHECK(s.run("category deactivate") == 0);
    CHECK(s.app.categories().list(pesa::Visibility::IncludeInactive)[0].isActive == false);
    CHECK_FALSE(contains(s.output(), "Home"));
    CHECK_FALSE(contains(s.output(), "Misc"));
}

TEST_CASE(
    "catalog shows categories with their items underneath, Misc last"
) {
    Session s;
    CHECK(s.run("catalog") == 0);
    CHECK(contains(s.output(), "Misc"));
    CHECK(contains(s.output(), "built in"));
    CHECK(contains(s.output(), "No items yet"));

    s.run("category add Food");
    s.run("category add Transport");
    s.run("item add Food 60 Milk");
    s.run("item add Transport 50 Matatu");
    s.out.str("");
    CHECK(s.run("catalog") == 0);
    const auto text = s.output();
    CHECK(contains(text, "Food   (category 2)")); // Misc took id 1 when the file was made
    CHECK(contains(text, "Transport   (category 3)"));
    CHECK(text.find("Food") < text.find("Milk")); // the item sits under its category
    CHECK(text.find("Milk") < text.find("Transport"));
    CHECK(text.find("Matatu") < text.find("Misc"));
    CHECK_FALSE(contains(text, "No items yet"));
}

TEST_CASE(
    "catalog add asks whether it is a category or an item"
) {
    Session s("2\n1\nBread\n55\n"); // an item; category 1; name; amount
    s.app.categories().add("Food");
    CHECK(s.run("catalog add") == 0);
    REQUIRE(s.app.items().list().size() == 1);
    CHECK(s.app.items().list()[0].name == "Bread");

    Session c("1\nHome\n"); // a category
    CHECK(c.run("catalog add") == 0);
    CHECK(userCategories(c.app)[0].name == "Home");
}

TEST_CASE(
    "catalog edit: pick something, then change it"
) {
    Session s("2\n2\n75\n"); // line 2 is Milk; action 2 is 'Change the amount'; 75
    const auto food = s.app.categories().add("Food");
    const auto milk = s.app.items().add({.categoryId = food, .name = "Milk", .defaultAmount = 60});
    CHECK(s.run("catalog edit") == 0);
    CHECK(s.app.items().find(milk)->defaultAmount == 75);
    CHECK(contains(s.output(), "Milk")); // the catalog is shown again afterwards

    Session add("1\n2\nBread\n55\n"); // pick the category; 'Add an item to it'; name; amount
    add.app.categories().add("Food");
    CHECK(add.run("catalog edit") == 0);
    CHECK(add.app.items().list()[0].name == "Bread");
}

TEST_CASE(
    "catalog edit on Misc only offers adding an item"
) {
    Session s("1\n1\nBank fee\n30\n"); // Misc is the only line; its only action; name; amount
    CHECK(s.run("catalog edit") == 0);
    REQUIRE(s.app.items().list().size() == 1);
    CHECK(s.app.items().list()[0].categoryName == "Misc");
}

TEST_CASE(
    "first launch asks how you are paid, then the pay, the payday and what you have"
) {
    const auto payday = pesa::Date::today().addDays(20).toString();
    Session s("1\n70,000\n" + payday + "\n12,000\n"); // regular; pay; payday; starting balance
    cli::runStartup(s.ctx, true);
    CHECK(s.app.settings().setupDone());
    CHECK(s.app.settings().incomeMode() == pesa::IncomeMode::Regular);
    CHECK(s.app.settings().expectedPay() == 70000);
    REQUIRE(s.app.periods().current().has_value());
    CHECK(s.app.periods().current()->expectedPayday == pesa::Date::parse(payday));
    CHECK(s.app.periods().summary(pesa::Date::today()).income == 12000);

    Session freelance("2\n\n"); // irregular; nothing in hand
    cli::runStartup(freelance.ctx, true);
    CHECK(freelance.app.settings().incomeMode() == pesa::IncomeMode::Irregular);
    CHECK_FALSE(freelance.app.periods().current()->expectedPayday.has_value());
    CHECK(contains(freelance.output(), "income add"));
}

TEST_CASE(
    "setup is not repeated, and one-liners never ask"
) {
    Session done("");
    done.app.completeSetup({.mode = pesa::IncomeMode::Irregular}, pesa::Date::today());
    cli::runStartup(done.ctx, true);
    CHECK(done.output().empty());

    Session script("");
    cli::runStartup(script.ctx, false); // a script run: no questions, nothing saved
    CHECK_FALSE(script.app.settings().setupDone());
    CHECK_FALSE(script.app.periods().current().has_value());
    CHECK(script.output().empty());
}

TEST_CASE(
    "status, delay and paid work from the command line"
) {
    Session s;
    const auto today = pesa::Date::today();
    s.app.completeSetup({
                            .mode = pesa::IncomeMode::Regular, .pay = 70000, .payday = today.addDays(10),
                            .startingBalance = 12000
                        }, today.addDays(-20));

    CHECK(s.run("status") == 0);
    CHECK(contains(s.output(), "Earned remaining"));
    CHECK(contains(s.output(), "12,000"));

    CHECK(s.run("delay 3") == 0);
    CHECK(s.app.periods().current()->expectedPayday == today.addDays(13));
    CHECK(s.run("delay 0") == 1);

    s.out.str("");
    CHECK(s.run("paid --amount 70,000") == 0);
    CHECK(contains(s.output(), "closed the period"));
    CHECK(s.app.savings().total() == 12000);
    CHECK(s.run("paid --amount 70,000") == 1); // same day again
}

TEST_CASE(
    "income and savings commands"
) {
    Session s;
    s.app.completeSetup({.mode = pesa::IncomeMode::Irregular}, pesa::Date::today());

    CHECK(s.run("income add 15,000 Dad") == 0);
    CHECK(s.app.periods().summary(pesa::Date::today()).income == 15000);

    CHECK(s.run("savings newpot Sacco --tied") == 0);
    CHECK(s.run("savings add 5000 --pot Sacco Contribution") == 0);
    CHECK(s.app.savings().total() == 5000);
    CHECK(s.run("savings add 100 --pot Nowhere") == 1);

    CHECK(s.run("savings move 2000 --pot sacco") == 0);
    CHECK(s.app.periods().summary(pesa::Date::today()).income == 17000);
    CHECK(contains(s.output(), "only has") == false);

    const auto entry = s.app.entries().addOneOff(pesa::Date::today(), "Barber", 500);
    CHECK(s.run("savings cover " + std::to_string(entry)) == 0);
    CHECK(s.app.entries().find(entry)->fund == pesa::FundedFrom::Savings);
    CHECK(s.run("savings cover") == 1);
    CHECK(s.run("savings") == 0);
    CHECK(s.run("savings ledger") == 0);
    CHECK(contains(s.output(), "Expense cover"));
}

TEST_CASE(
    "the shell keeps going after errors and leaves on exit"
) {
    Session s(
        "category add Food\nitem add Food 60 Milk\nbogus\nitem add Food 0 X\n\ncatalog\nexit\ncategory add Never\n");
    CHECK(cli::runShell(s.ctx) == 0);
    CHECK(contains(s.output(), "pesa> "));
    CHECK(contains(s.output(), "Milk"));
    CHECK(contains(s.errors(), "Unknown command"));
    CHECK(contains(s.errors(), "more than 0"));
    CHECK(userCategories(s.app).size() == 1); // nothing after 'exit' ran
}

TEST_CASE(
    "the shell ends cleanly when the input ends"
) {
    Session s("category add Food\n");
    CHECK(cli::runShell(s.ctx) == 0);
    CHECK(userCategories(s.app).size() == 1);
}

TEST_CASE(
    "day shows today when no date is given"
) {
    Session s;
    CHECK(s.run("day") == 0);
    CHECK(contains(s.output(), pesa::Date::today().toString()));
    CHECK(contains(s.output(), "No entries."));
}

TEST_CASE(
    "settings can switch auto-post off"
) {
    Session s;
    CHECK(s.run("settings autopost off") == 0);
    CHECK_FALSE(s.app.settings().autoPostEnabled());
    CHECK(s.run("settings autopost maybe") == 1);
}

TEST_CASE(
    "help lists the commands"
) {
    Session s;
    CHECK(s.run("help") == 0);
    CHECK(contains(s.output(), "catalog edit"));
    CHECK(contains(s.output(), "entry oneoff"));
}

TEST_CASE(
    "job says what it did, and respects the auto-post switch"
) {
    Session s;
    CHECK(s.run("job") == 0);
    CHECK(contains(s.output(), "Nothing new to post"));

    s.out.str("");
    CHECK(s.run("settings autopost off") == 0);
    CHECK(s.run("job") == 0);
    CHECK(contains(s.output(), "Auto-post is off"));
}

TEST_CASE(
    "settings postafter changes when today's routine posts"
) {
    Session s;
    CHECK(s.run("settings postafter 18:30") == 0);
    CHECK(s.app.settings().postAfter().toString() == "18:30");
    CHECK(s.run("settings postafter noon") == 1);
}

TEST_CASE("goals can be added, listed, changed, bought and deleted from the command line") {
    Session s;
    s.app.completeSetup({.mode = pesa::IncomeMode::Irregular, .startingBalance = 10000}, pesa::Date::today());

    CHECK(s.run("goal add 30,000 Electric guitar --priority 1 --savings") == 0);
    CHECK(s.run("goal add 6500 Curtains") == 0); // priority 11: the lowest so far, plus 10
    CHECK(s.app.goals().find(2)->priority == 11);
    s.out.str("");
    CHECK(s.run("goal list") == 0);
    CHECK(contains(s.output(), "Electric guitar"));
    CHECK(contains(s.output(), "short by 30,000")); // Main savings is empty

    CHECK(s.run("goal edit 2 --amount 7000 --priority 5") == 0);
    CHECK(s.app.goals().find(2)->amount == 7000);
    CHECK(s.run("goal edit 2") == 1); // nothing to change

    CHECK(s.run("goal buy 2 --how earned") == 0);
    CHECK(s.app.goals().find(2)->isBought());
    CHECK(s.app.days().view(pesa::Date::today()).entries[0].name == "Curtains");
    CHECK(s.run("goal buy 2 --how earned") == 1); // already bought
    CHECK(s.run("goal buy 1 --how bogus") == 1);

    CHECK(s.run("goal delete 1") == 0);
    s.out.str("");
    CHECK(s.run("goal list --bought") == 0);
    CHECK(contains(s.output(), "Curtains"));
}

TEST_CASE("funds follow a pot, and a goal due on launch is bought or listed") {
    Session s;
    const auto today = pesa::Date::today();
    s.app.completeSetup({.mode = pesa::IncomeMode::Irregular, .startingBalance = 20000}, today);

    CHECK(s.run("savings newpot Emergency --locked") == 0);
    CHECK(s.run("savings add 50,000 --pot Emergency Start") == 0);
    CHECK(s.run("fund add 200,000 Emergency fund --pot Emergency") == 0);
    s.out.str("");
    CHECK(s.run("fund list") == 0);
    CHECK(contains(s.output(), "25%"));

    s.app.goals().add({
        .name = "Desk", .amount = 12000, .priority = 1, .dueDate = today.previousDay(),
        .fundedFrom = pesa::FundedFrom::Earned, .autoBuy = true
    });
    s.app.goals().add({.name = "Curtains", .amount = 6500, .priority = 2, .dueDate = today});
    s.out.str("");
    cli::runStartup(s.ctx, true);
    CHECK(contains(s.output(), "Bought goal Desk"));
    CHECK(contains(s.output(), "Goal due: Curtains"));
    CHECK(s.app.goals().find(1)->isBought());
    CHECK_FALSE(s.app.goals().find(2)->isBought());
}

TEST_CASE("auto-cover and the payday policy are settings, and the warning lists the alternatives") {
    Session s;
    s.app.completeSetup({.mode = pesa::IncomeMode::Irregular, .startingBalance = 1000}, pesa::Date::today());

    CHECK(s.run("entry oneoff today 5000 Laptop") == 0); // earned goes below zero; auto-cover is off
    CHECK(s.run("status") == 0);
    CHECK(contains(s.output(), "settings autocover on"));
    CHECK(s.run("settings autocover on") == 0);
    CHECK(s.app.settings().autoCoverEnabled());

    s.out.str("");
    CHECK(s.run("entry oneoff today 700 Snacks") == 0);
    CHECK(contains(s.output(), "paid from Main savings"));
    CHECK(contains(s.output(), "below zero")); // Main savings went negative: warned

    CHECK(s.run("settings shortfall carry") == 0);
    CHECK(s.app.settings().shortfallPolicy() == pesa::ShortfallPolicy::CarryForward);
    CHECK(s.run("settings shortfall bogus") == 1);
}

TEST_CASE("q leaves, < goes back one question, and the earlier answer is the default") {
    Session back("2026-11-23\nWrong\n<\nRight\n500\n1\n");
    // date, name, amount: back to the name, new name, amount, earned
    CHECK(back.run("entry oneoff") == 0);
    const auto day = back.app.days().view(kDay);
    REQUIRE(day.entries.size() == 1);
    CHECK(day.entries[0].name == "Right");
    CHECK(day.entries[0].amount == 500);

    Session quit("2026-11-23\nq\n"); // q at the second question
    CHECK(quit.run("entry oneoff") == 1);
    CHECK(contains(quit.output(), "Cancelled."));
    CHECK(quit.app.days().view(kDay).entries.empty());

    Session first("<\n"); // nothing before the first question: leave
    CHECK(first.run("entry oneoff") == 1);
}

TEST_CASE("the menu runs areas, q inside returns to it, and q at the menu leaves") {
    Session s("1\nq\n"); // "See today", then q at the main menu
    CHECK(s.run("menu") == 0);
    CHECK(contains(s.output(), "Main menu"));
    CHECK(contains(s.output(), "No entries."));

    Session inside("2\n2\n\nq\nq\n");
    // Entries > Add a one-off > Enter (today) > q at the name question (back to menu) > q (leave)                            // Entries > Add a one-off > q (back to menu) > q (leave)
    CHECK(inside.run("menu") == 0);
    CHECK(contains(inside.output(), "What was it for"));
}

TEST_CASE("a new savings pot asks whether it is open, locked or tied") {
    Session s("Sacco\n3\n");
    CHECK(s.run("savings newpot") == 0);
    CHECK(s.app.savings().pots()[1].kind == pesa::PotKind::Tied);
    CHECK(s.run("savings newpot Emergency --locked") == 0); // a flag skips the question
    CHECK(s.app.savings().pots()[2].kind == pesa::PotKind::Locked);
}

TEST_CASE("a rule can take several items and whole categories in one command") {
    Session s;
    s.run("category add Transport");
    s.run("category add Food");
    s.run("item add Transport 50 Matatu");
    s.run("item add Transport 150 Tuktuk");
    s.run("item add Food 60 Tea");
    s.run("item add Food 250 Lunch");

    CHECK(s.run("rule add Commute --days weekdays --items 1,2 --category Food") == 0);
    CHECK(s.app.rules().find(1)->items.size() == 4);
    CHECK(s.run("rule add Empty --days daily --category Misc") == 1); // Misc has no items yet
    CHECK(s.errors().find("has no items yet") != std::string::npos);

    CHECK(s.run("rule add Bills --monthly 1 --item 3") == 0);
    CHECK(s.run("rule additem 2 1 4") == 0); // several ids, one command
    CHECK(s.app.rules().find(2)->items.size() == 3);
    CHECK(s.run("rule removeitem 2 1 4") == 0);
    CHECK(s.app.rules().find(2)->items.size() == 1);
    CHECK(s.run("rule removeitem 2 1") == 1); // not in the rule any more
}

TEST_CASE("short-lived categories and items can be set from the command line") {
    Session s;
    CHECK(s.run("category add Trip --until 2026-12-31") == 0);
    CHECK(s.app.categories().find(2)->expiresOn == pesa::Date::parse("2026-12-31"));
    CHECK(s.run("item add Trip 500 Hotel --until 2026-12-30") == 0);
    CHECK(s.app.items().find(1)->expiresOn == pesa::Date::parse("2026-12-30"));

    s.out.str("");
    CHECK(s.run("catalog") == 0);
    CHECK(contains(s.output(), "until 2026-12-31"));

    CHECK(s.run("item forever 1") == 0);
    CHECK_FALSE(s.app.items().find(1)->expiresOn.has_value());
    CHECK(s.run("category until 2 2027-02-01") == 0);
    CHECK(s.run("category until 1 2027-02-01") == 1); // Misc cannot be short-lived
}

TEST_CASE("pay raises, expected income and the arrival question work from the command line") {
    Session s;
    const auto today = pesa::Date::today();
    s.app.completeSetup({.mode = pesa::IncomeMode::Regular, .pay = 70000, .payday = today.addDays(20)}, today);

    CHECK(s.run("income pay 75,000 --from " + today.addDays(40).toString()) == 0);
    CHECK(s.app.income().payHistory().size() == 2);
    CHECK(s.run("income pay 1000 --from " + today.toString()) == 1); // earlier than the newest row

    const auto yesterday = today.previousDay();
    CHECK(s.run("income source add 4,500 Dividends --often yearly --next " + yesterday.toString() +
        " --pot \"Main savings\"") == 0);
    s.out.str("");
    cli::runStartup(s.ctx, false); // a script run: a reminder, no questions
    CHECK(s.errors().find("Expected income is due: Dividends") != std::string::npos);
    CHECK(s.app.savings().total() == 0);

    Session ask("1\n\n"); // "Yes, it arrived", then Enter for the usual amount
    ask.app.completeSetup({.mode = pesa::IncomeMode::Irregular}, today);
    ask.app.income().addSource({
        .name = "Dividends", .usualAmount = 4500, .frequency = pesa::Frequency::Yearly,
        .nextExpected = yesterday, .potId = ask.app.savings().mainPot()
    });
    CHECK(ask.run("income due") == 0);
    CHECK(ask.app.savings().total() == 4500);
    CHECK(ask.app.income().sources()[0].nextExpected == yesterday.addMonths(12));
}

TEST_CASE("colours tell entries apart") {
    cli::Table table({{"Name"}});
    table.addRow({"auto"}, cli::Style::Cyan);
    table.addRow({"one-off"}, cli::Style::Magenta);
    table.addRow({"issue"}, cli::Style::Red);
    const auto text = table.render(true);
    CHECK(text.find("\x1b[36m") != std::string::npos);
    CHECK(text.find("\x1b[35m") != std::string::npos);
    CHECK(text.find("\x1b[31m") != std::string::npos);
    CHECK(table.render(false).find('\x1b') == std::string::npos);
}
