// FTracker/tests/test_cli_commands.cpp
#include <doctest/doctest.h>

#include <cli/commands.h>

#include <sstream>

namespace {

// A pretend terminal: `input` is what the person types, output is collected.
struct Session {
    pesa::Pesa         app{":memory:"};
    std::istringstream in;
    std::ostringstream out;
    std::ostringstream err;
    cli::Console       io;
    cli::Context       ctx;

    explicit Session(const std::string& input = "") : in(input), io(in, out, err, false), ctx{app, io} {}

    int run(const std::string& line) { return cli::runGuarded(ctx, cli::splitLine(line)); }
    [[nodiscard]] std::string output() const { return out.str(); }
    [[nodiscard]] std::string errors() const { return err.str(); }
};

bool contains(const std::string& text, const std::string& part) { return text.find(part) != std::string::npos; }

// Misc is always there, so tests about "my categories" skip it.
std::vector<pesa::Category> userCategories(const pesa::Pesa& app) {
    std::vector<pesa::Category> out;
    for (auto& c : app.categories().list()) {
        if (!c.isSystem) out.push_back(std::move(c));
    }
    return out;
}

const pesa::Date kDay = pesa::Date::parse("2026-11-23");

} // namespace

TEST_CASE("one-liners still work, and a one-off lands in Misc") {
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
    CHECK(day.entries[1].categoryName == "Misc");   // Food first, Misc last
    CHECK(s.app.items().list()[1].name == "Fresh bread");
    CHECK(contains(s.output(), "Uncle Bill's wedding"));   // the day is shown after each change
    CHECK(contains(s.output(), "Spent 560"));
}

TEST_CASE("errors go to the error stream and give a failing exit code") {
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

TEST_CASE("Misc cannot be renamed or deleted from the command line") {
    Session s;
    const auto misc = std::to_string(s.app.categories().misc());
    CHECK(s.run("category rename " + misc + " Other") == 1);
    CHECK(contains(s.errors(), "built in"));
    CHECK(s.run("category delete " + misc) == 1);
}

TEST_CASE("category add asks for the name") {
    Session s("Food\n");
    CHECK(s.run("category add") == 0);
    REQUIRE(userCategories(s.app).size() == 1);
    CHECK(userCategories(s.app)[0].name == "Food");
    CHECK(contains(s.output(), "Category name: "));
}

TEST_CASE("a blank answer or the end of input cancels") {
    Session blank("\n");
    CHECK(blank.run("category add") == 1);
    CHECK(contains(blank.output(), "Cancelled."));
    CHECK(userCategories(blank.app).empty());

    Session ended("");
    CHECK(ended.run("category add") == 1);
    CHECK(userCategories(ended.app).empty());

    Session quit("q\n");
    quit.app.categories().add("Food");
    CHECK(quit.run("category rename") == 1);   // "q" at a numbered list backs out
}

TEST_CASE("item add walks through category, name and amount") {
    Session s("1\nMilk\n60\n");
    s.app.categories().add("Food");
    CHECK(s.run("item add") == 0);

    const auto items = s.app.items().list();
    REQUIRE(items.size() == 1);
    CHECK(items[0].name == "Milk");
    CHECK(items[0].defaultAmount == 60);
    CHECK(contains(s.output(), "1) Food"));
}

TEST_CASE("wrong answers are explained and asked again") {
    Session s("1\nMilk\nabc\n0\n60\n");   // bad amount, zero, then 60
    s.app.categories().add("Food");
    CHECK(s.run("item add") == 0);
    CHECK(contains(s.output(), "whole number"));
    CHECK(contains(s.output(), "more than 0"));
    CHECK(s.app.items().list()[0].defaultAmount == 60);
}

TEST_CASE("entry add picks the day, category and item, and Enter takes the defaults") {
    Session s("2026-11-23\n1\n1\n\n\n");   // date, category 1, item 1, default amount, default 'earned'
    const auto food = s.app.categories().add("Food");
    s.app.items().add({.categoryId = food, .name = "Milk", .defaultAmount = 60});
    CHECK(s.run("entry add") == 0);

    const auto day = s.app.days().view(kDay);
    REQUIRE(day.entries.size() == 1);
    CHECK(day.entries[0].amount == 60);
    CHECK(day.entries[0].fund == pesa::FundedFrom::Earned);
    CHECK(contains(s.output(), "Spent 60"));
}

TEST_CASE("entry oneoff asks for everything, and Enter on the category means Misc") {
    Session s("2026-11-23\nUncle Bill's wedding\n1,500\n\n2\n");   // date, name, amount, category (Enter), savings
    s.app.categories().add("Food");
    CHECK(s.run("entry oneoff") == 0);

    const auto day = s.app.days().view(kDay);
    REQUIRE(day.entries.size() == 1);
    CHECK(day.entries[0].isOneOff());
    CHECK(day.entries[0].amount == 1500);
    CHECK(day.entries[0].fund == pesa::FundedFrom::Savings);
    CHECK(day.entries[0].categoryName == "Misc");
    CHECK(s.app.items().list().empty());                  // never touches the catalog
}

TEST_CASE("deleting an entry in guided mode asks first") {
    Session no("2026-11-23\n1\nn\n");
    const auto food = no.app.categories().add("Food");
    no.app.entries().addOneOff(kDay, food, "Gift", 100);
    CHECK(no.run("entry delete") == 0);
    CHECK(no.app.days().view(kDay).entries.size() == 1);   // said no

    Session yes("2026-11-23\n1\ny\n");
    const auto f2 = yes.app.categories().add("Food");
    yes.app.entries().addOneOff(kDay, f2, "Gift", 100);
    CHECK(yes.run("entry delete") == 0);
    CHECK(yes.app.days().view(kDay).entries.empty());
}

TEST_CASE("pausing offers only categories that can be paused") {
    Session s("1\n");                       // Food is the only one: Home is paused, Misc is built in
    s.app.categories().add("Food");
    const auto home = s.app.categories().add("Home");
    s.app.categories().setActive(home, false);
    CHECK(s.run("category deactivate") == 0);
    CHECK(s.app.categories().list(pesa::Visibility::IncludeInactive)[0].isActive == false);
    CHECK_FALSE(contains(s.output(), "Home"));
    CHECK_FALSE(contains(s.output(), "Misc"));
}

TEST_CASE("catalog shows categories with their items underneath, Misc last") {
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
    CHECK(contains(text, "Food   (category 2)"));       // Misc took id 1 when the file was made
    CHECK(contains(text, "Transport   (category 3)"));
    CHECK(text.find("Food") < text.find("Milk"));        // the item sits under its category
    CHECK(text.find("Milk") < text.find("Transport"));
    CHECK(text.find("Matatu") < text.find("Misc"));
    CHECK_FALSE(contains(text, "No items yet"));
}

TEST_CASE("catalog add asks whether it is a category or an item") {
    Session s("2\n1\nBread\n55\n");        // an item; category 1; name; amount
    s.app.categories().add("Food");
    CHECK(s.run("catalog add") == 0);
    REQUIRE(s.app.items().list().size() == 1);
    CHECK(s.app.items().list()[0].name == "Bread");

    Session c("1\nHome\n");                // a category
    CHECK(c.run("catalog add") == 0);
    CHECK(userCategories(c.app)[0].name == "Home");
}

TEST_CASE("catalog edit: pick something, then change it") {
    Session s("2\n2\n75\n");               // line 2 is Milk; action 2 is 'Change the amount'; 75
    const auto food = s.app.categories().add("Food");
    const auto milk = s.app.items().add({.categoryId = food, .name = "Milk", .defaultAmount = 60});
    CHECK(s.run("catalog edit") == 0);
    CHECK(s.app.items().find(milk)->defaultAmount == 75);
    CHECK(contains(s.output(), "Milk"));   // the catalog is shown again afterwards

    Session add("1\n2\nBread\n55\n");      // pick the category; 'Add an item to it'; name; amount
    add.app.categories().add("Food");
    CHECK(add.run("catalog edit") == 0);
    CHECK(add.app.items().list()[0].name == "Bread");
}

TEST_CASE("catalog edit on Misc only offers adding an item") {
    Session s("1\n1\nBank fee\n30\n");     // Misc is the only line; its only action; name; amount
    CHECK(s.run("catalog edit") == 0);
    REQUIRE(s.app.items().list().size() == 1);
    CHECK(s.app.items().list()[0].categoryName == "Misc");
}

TEST_CASE("first launch asks for the pay, and the payday only if there is a pay") {
    Session s("70,000\n2026-12-24\n");
    cli::runStartup(s.ctx, true);
    CHECK(s.app.settings().setupDone());
    CHECK(s.app.settings().expectedPay() == 70000);
    CHECK(s.app.settings().expectedPayday() == pesa::Date::parse("2026-12-24"));

    Session varies("\n");                  // blank = my income varies
    cli::runStartup(varies.ctx, true);
    CHECK(varies.app.settings().setupDone());
    CHECK_FALSE(varies.app.settings().expectedPay().has_value());
    CHECK(contains(varies.output(), "Projections stay off"));
}

TEST_CASE("setup is not repeated, and one-liners never ask") {
    Session done("");
    done.app.settings().completeSetup(std::nullopt, std::nullopt);
    cli::runStartup(done.ctx, true);
    CHECK(done.output().empty());

    Session script("");
    cli::runStartup(script.ctx, false);    // a script run: no questions, nothing saved
    CHECK_FALSE(script.app.settings().setupDone());
    CHECK(script.output().empty());
}

TEST_CASE("the shell keeps going after errors and leaves on exit") {
    Session s("category add Food\nitem add Food 60 Milk\nbogus\nitem add Food 0 X\n\ncatalog\nexit\ncategory add Never\n");
    CHECK(cli::runShell(s.ctx) == 0);
    CHECK(contains(s.output(), "pesa> "));
    CHECK(contains(s.output(), "Milk"));
    CHECK(contains(s.errors(), "Unknown command"));
    CHECK(contains(s.errors(), "more than 0"));
    CHECK(userCategories(s.app).size() == 1);              // nothing after 'exit' ran
}

TEST_CASE("the shell ends cleanly when the input ends") {
    Session s("category add Food\n");
    CHECK(cli::runShell(s.ctx) == 0);
    CHECK(userCategories(s.app).size() == 1);
}

TEST_CASE("day shows today when no date is given") {
    Session s;
    CHECK(s.run("day") == 0);
    CHECK(contains(s.output(), pesa::Date::today().toString()));
    CHECK(contains(s.output(), "No entries."));
}

TEST_CASE("settings can switch auto-post off") {
    Session s;
    CHECK(s.run("settings autopost off") == 0);
    CHECK_FALSE(s.app.settings().autoPostEnabled());
    CHECK(s.run("settings autopost maybe") == 1);
}

TEST_CASE("help lists the commands") {
    Session s;
    CHECK(s.run("help") == 0);
    CHECK(contains(s.output(), "catalog edit"));
    CHECK(contains(s.output(), "entry oneoff"));
}