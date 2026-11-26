// FTracker/tests/test_cli_text.cpp
#include <doctest/doctest.h>

#include <cli/display.h>
#include <cli/text.h>

using cli::Args;
using cli::Table;

TEST_CASE (

"splitLine splits on spaces and keeps quoted phrases together"
)
 {
    CHECK(cli::splitLine("item add Food 60 Milk") == Args{"item", "add", "Food", "60", "Milk"});
    CHECK(cli::splitLine("  item   add  ") == Args{"item", "add"});
    CHECK(cli::splitLine("item add Food 60 \"Fresh milk\"") == Args{"item", "add", "Food", "60", "Fresh milk"});
    CHECK(cli::splitLine("rename 1 'Eating out'") == Args{"rename", "1", "Eating out"});
    CHECK(cli::splitLine("").empty());
}

TEST_CASE (

"an apostrophe inside a word is just a letter"
)
 {
    CHECK(cli::splitLine("oneoff Uncle Bill's wedding") == Args{"oneoff", "Uncle", "Bill's", "wedding"});
    CHECK_THROWS_AS((void)cli::splitLine("rename \"oops"), pesa::Error);
}

TEST_CASE (

"numbers, amounts and dates are read strictly"
)
 {
    CHECK(cli::parseInt("42") == 42);
    CHECK_FALSE(cli::parseInt("4x").has_value());
    CHECK_FALSE(cli::parseInt("").has_value());
    CHECK(cli::parseAmount("1,250") == 1250);
    CHECK_THROWS_AS((void)cli::parseAmount("abc"), pesa::Error);
    CHECK_THROWS_AS((void)cli::parseId("x", "item"), pesa::Error);
    CHECK(cli::parseDate("2026-11-23") == pesa::Date::parse("2026-11-23"));
    CHECK(cli::parseDate("Today") == pesa::Date::today());
    CHECK(cli::parseDate("tomorrow") == pesa::Date::today().nextDay());
    CHECK_THROWS_AS((void)cli::parseDate("someday"), pesa::Error);
}

TEST_CASE (

"days can be typed back from a set"
)
 {
    for (const auto& days : {pesa::WeekdaySet::everyDay(), pesa::WeekdaySet::workDays(), pesa::WeekdaySet::weekend(),
                             pesa::WeekdaySet{}, pesa::WeekdaySet{pesa::Weekday::Mon, pesa::Weekday::Wed}}) {
        CHECK(pesa::WeekdaySet::parse(cli::daysInputText(days)) == days);
    }
}

TEST_CASE (

"money gets thousands separators"
)
 {
    CHECK(cli::formatMoney(0) == "0");
    CHECK(cli::formatMoney(999) == "999");
    CHECK(cli::formatMoney(1000) == "1,000");
    CHECK(cli::formatMoney(1234567) == "1,234,567");
    CHECK(cli::formatMoney(-1500) == "-1,500");
}

TEST_CASE (

"width counts letters, and long text is cut with dots"
)
 {
    CHECK(cli::displayWidth("Milk") == 4);
    CHECK(cli::displayWidth("caf\xC3\xA9") == 4);                       // "café": 5 bytes, 4 letters
    CHECK(cli::truncate("short", 10) == "short");
    CHECK(cli::truncate("abcdefghijklmnop", 10) == "abcdefg...");
    CHECK(cli::truncate("caf\xC3\xA9 au lait con azucar", 8) == "caf\xC3\xA9 ...");   // 5 letters + dots, never half a letter
}

TEST_CASE (

"flags and options can be typed anywhere"
)
 {
    Args args{"item", "list", "--all", "Food"};
    CHECK(cli::takeFlag(args, "--all"));
    CHECK(args == Args{"item", "list", "Food"});
    CHECK_FALSE(cli::takeFlag(args, "--all"));

    Args withOption{"item", "add", "--days", "weekdays", "Food"};
    CHECK(cli::takeOption(withOption, "--days") == "weekdays");
    CHECK(withOption == Args{"item", "add", "Food"});
    Args dangling{"item", "--days"};
    CHECK_THROWS_AS((void)cli::takeOption(dangling, "--days"), pesa::Error);

    Args both{"--all", "--deleted"};
    CHECK_THROWS_AS((void)cli::takeVisibility(both), pesa::Error);
}

TEST_CASE (

"ArgReader hands out words, then the rest"
)
 {
    const Args args{"item", "add", "Food", "60", "Fresh", "milk"};
    cli::ArgReader reader(args, 2);
    CHECK(reader.next() == "Food");
    CHECK(reader.next() == "60");
    CHECK(reader.rest() == "Fresh milk");
    CHECK_FALSE(reader.next().has_value());
    CHECK_FALSE(reader.rest().has_value());
}

TEST_CASE (

"a table sizes its columns to the content"
)
 {
    Table table({{"ID", Table::Align::Right}, {"Name"}, {"Amount", Table::Align::Right}});
    table.addRow({"1", "Milk", "60"});
    table.addRow({"12", "Bread and butter", "1,250"});

    const std::string expected =
        "  ID  Name              Amount\n"
        "  --  ----------------  ------\n"
        "   1  Milk                  60\n"
        "  12  Bread and butter   1,250\n";
    CHECK(table.render(false) == expected);
}

TEST_CASE (

"long cells are cut, sections break the table, and colour is optional"
)
 {
    Table table({{"Name", Table::Align::Left, 8}, {"N", Table::Align::Right}});
    table.addSection("Food");
    table.addRow({"A very long name", "5"}, cli::Style::Yellow);

    const std::string plain = table.render(false);
    CHECK(plain.find("A ver...") != std::string::npos);
    CHECK(plain.find("Food") != std::string::npos);
    CHECK(plain.find('\x1b') == std::string::npos);            // no colour codes when off

    CHECK(table.render(true).find("\x1b[33m") != std::string::npos);   // yellow row when on
}
