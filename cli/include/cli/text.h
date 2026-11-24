// FTracker/cli/include/cli/text.h
// Plain text helpers: splitting a typed line, reading numbers and dates, formatting.
#pragma once

#include <pesa/pesa.h>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace cli {

using Args = std::vector<std::string>;

// "item add Food 60 \"Fresh milk\"" -> {item, add, Food, 60, Fresh milk}.
// A quote only opens at the START of a word, so Uncle Bill's works as typed.
Args splitLine(std::string_view line);

std::string joinFrom(const Args& args, std::size_t from);          // words -> one string
std::string toLower(std::string text);

std::optional<std::int64_t> parseInt(std::string_view text);       // whole numbers only
std::int64_t parseId(std::string_view text, const char* what);     // throws a friendly Error
pesa::Money  parseAmount(std::string_view text);                   // allows 1,250
pesa::Date   parseDate(std::string_view text);                     // today | tomorrow | yesterday | 2026-11-23

// The text you would type to get this set back, e.g. "weekdays" or "mon,wed".
std::string daysInputText(pesa::WeekdaySet days);

std::string formatMoney(pesa::Money amount);                       // 1250 -> "1,250"
std::size_t displayWidth(std::string_view text);                   // letters, not bytes (UTF-8 aware)
std::string truncate(std::string_view text, std::size_t maxWidth); // cuts and adds "..."

// Flags and options pulled out of the arguments, wherever they are typed.
bool takeFlag(Args& args, const std::string& flag);                          // --all
std::optional<std::string> takeOption(Args& args, const std::string& flag);  // --days X
pesa::Visibility takeVisibility(Args& args);                                 // --all / --deleted

// Hands out the words after the command one at a time, or nothing when they ran out.
class ArgReader {
public:
    ArgReader(const Args& args, std::size_t start) : args_(args), pos_(start) {}

    std::optional<std::string> next();   // the next single word
    std::optional<std::string> rest();   // all remaining words joined with spaces

private:
    const Args& args_;
    std::size_t pos_;
};

} // namespace cli
