// FTracker/cli/text.cpp
#include <cli/text.h>

#include <algorithm>
#include <cctype>
#include <charconv>

namespace cli {
    Args splitLine(std::string_view line) {
        Args out;
        std::string word;
        bool inWord = false;
        char quote = 0;

        for (const char c: line) {
            if (quote != 0) {
                // inside "quotes": keep everything
                if (c == quote) quote = 0;
                else word += c;
                continue;
            }
            if (std::isspace(static_cast<unsigned char>(c))) {
                if (inWord) {
                    out.push_back(word);
                    word.clear();
                    inWord = false;
                }
                continue;
            }
            if (!inWord && (c == '"' || c == '\'')) {
                quote = c;
                inWord = true;
                continue;
            }
            word += c;
            inWord = true;
        }
        if (quote != 0) throw pesa::Error("A quote was opened but never closed.");
        if (inWord) out.push_back(word);
        return out;
    }

    std::string joinFrom(const Args &args, std::size_t from) {
        std::string out;
        for (std::size_t i = from; i < args.size(); ++i) {
            if (i > from) out += ' ';
            out += args[i];
        }
        return out;
    }

    std::string toLower(std::string text) {
        std::ranges::transform(text, text.begin(),
                               [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return text;
    }

    std::optional<std::int64_t> parseInt(std::string_view text) {
        std::int64_t value = 0;
        const auto [end, ec] = std::from_chars(text.data(), text.data() + text.size(), value);
        if (text.empty() || ec != std::errc{} || end != text.data() + text.size()) return std::nullopt;
        return value;
    }

    std::int64_t parseId(std::string_view text, const char *what) {
        const auto id = parseInt(text);
        if (!id) throw pesa::Error(std::string("Not a valid ") + what + " id: '" + std::string(text) + "'.");
        return *id;
    }

    pesa::Money parseAmount(std::string_view text) {
        std::string cleaned;
        for (const char c: text) if (c != ',') cleaned += c; // "1,250" is fine
        const auto amount = parseInt(cleaned);
        if (!amount) throw pesa::Error("The amount must be a whole number, got '" + std::string(text) + "'.");
        return *amount;
    }

    pesa::Date parseDate(std::string_view text) {
        const std::string t = toLower(std::string(text));
        if (t == "today") return pesa::Date::today();
        if (t == "tomorrow") return pesa::Date::today().nextDay();
        if (t == "yesterday") return pesa::Date::today().previousDay();
        return pesa::Date::parse(text);
    }

    std::string daysInputText(pesa::WeekdaySet days) {
        if (days == pesa::WeekdaySet::everyDay()) return "daily";
        if (days == pesa::WeekdaySet::workDays()) return "weekdays";
        if (days == pesa::WeekdaySet::weekend()) return "weekend";
        if (days.empty()) return "none";
        return toLower(days.toString()); // "mon,wed"
    }

    // std::string formatMoney(pesa::Money amount) {
    //     const bool negative = amount < 0;
    //     const std::string digits = std::to_string(negative ? -amount : amount);
    //     std::string out;
    //     for (std::size_t i = 0; i < digits.size(); ++i) {
    //         if (i != 0 && (digits.size() - i) % 3 == 0) out += ',';
    //         out += digits[i];
    //     }
    //     return negative ? "-" + out : out;
    // }
    std::string formatMoney(pesa::Money amount) { return pesa::formatMoney(amount); }

    std::size_t displayWidth(std::string_view text) {
        std::size_t width = 0;
        for (const char c: text) {
            if ((static_cast<unsigned char>(c) & 0xC0) != 0x80) ++width; // skip UTF-8 continuation bytes
        }
        return width;
    }

    std::string truncate(std::string_view text, std::size_t maxWidth) {
        if (displayWidth(text) <= maxWidth) return std::string(text);

        const std::size_t keep = maxWidth > 3 ? maxWidth - 3 : maxWidth;
        std::size_t letters = 0;
        std::size_t bytes = 0;
        while (bytes < text.size() && letters < keep) {
            std::size_t len = 1; // one letter may take several bytes
            while (bytes + len < text.size() &&
                   (static_cast<unsigned char>(text[bytes + len]) & 0xC0) == 0x80)
                ++len;
            bytes += len;
            ++letters;
        }
        std::string out(text.substr(0, bytes));
        if (maxWidth > 3) out += "...";
        return out;
    }

    bool takeFlag(Args &args, const std::string &flag) {
        const auto it = std::ranges::find(args, flag);
        if (it == args.end()) return false;
        args.erase(it);
        return true;
    }

    std::optional<std::string> takeOption(Args &args, const std::string &flag) {
        const auto it = std::ranges::find(args, flag);
        if (it == args.end()) return std::nullopt;
        if (it + 1 == args.end()) throw pesa::Error("Missing value after " + flag + ".");
        std::string value = *(it + 1);
        args.erase(it, it + 2);
        return value;
    }

    pesa::Visibility takeVisibility(Args &args) {
        const bool all = takeFlag(args, "--all");
        const bool deleted = takeFlag(args, "--deleted");
        if (all && deleted) throw pesa::Error("Use either --all or --deleted, not both.");
        if (deleted) return pesa::Visibility::DeletedOnly;
        if (all) return pesa::Visibility::IncludeInactive;
        return pesa::Visibility::ActiveOnly;
    }

    std::optional<std::string> ArgReader::next() {
        if (pos_ >= args_.size()) return std::nullopt;
        return args_[pos_++];
    }

    std::optional<std::string> ArgReader::rest() {
        if (pos_ >= args_.size()) return std::nullopt;
        std::string joined = joinFrom(args_, pos_);
        pos_ = args_.size();
        return joined;
    }
} // namespace cli
