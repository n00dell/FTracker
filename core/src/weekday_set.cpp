// FTracker/core/src/weekday_set.cpp
#include <pesa/common.h>

#include <algorithm>
#include <array>
#include <cctype>

namespace pesa {
    namespace {
        constexpr std::array<std::string_view, 7> kShortNames = {"Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun"};
        constexpr std::array<std::string_view, 7> kLongLower = {
            "monday", "tuesday", "wednesday", "thursday",
            "friday", "saturday", "sunday"
        };

        std::string toLower(std::string_view s) {
            std::string out(s);
            std::ranges::transform(out, out.begin(),
                                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            return out;
        }

        std::string_view trim(std::string_view s) {
            while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) s.remove_prefix(1);
            while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.remove_suffix(1);
            return s;
        }

        WeekdaySet parseToken(const std::string &t) {
            if (t == "daily" || t == "everyday") return WeekdaySet::everyDay();
            if (t == "weekdays" || t == "workdays") return WeekdaySet::workDays();
            if (t == "weekend") return WeekdaySet::weekend();
            if (t == "none") return {};
            for (std::size_t i = 0; i < kLongLower.size(); ++i) {
                if (t == kLongLower[i] || t == kLongLower[i].substr(0, 3)) {
                    return WeekdaySet{static_cast<Weekday>(i)};
                }
            }
            throw Error("Unknown day '" + t + "'. Use mon,tue,... or daily, weekdays, weekend, none.");
        }
    } // namespace

    std::string WeekdaySet::toString() const {
        if (bits_ == 0b1111111) return "Every day";
        if (bits_ == 0b0011111) return "Mon-Fri";
        if (bits_ == 0) return "Never";

        std::string out;
        for (std::size_t i = 0; i < kShortNames.size(); ++i) {
            if (contains(static_cast<Weekday>(i))) {
                if (!out.empty()) out += ',';
                out += kShortNames[i];
            }
        }
        return out;
    }

    WeekdaySet WeekdaySet::parse(std::string_view text) {
        WeekdaySet result;
        bool sawToken = false;

        while (true) {
            // split on commas
            const auto comma = text.find(',');
            const std::string token = toLower(trim(text.substr(0, comma)));
            if (!token.empty()) {
                sawToken = true;
                result.bits_ |= parseToken(token).bits_;
            }
            if (comma == std::string_view::npos) break;
            text.remove_prefix(comma + 1);
        }

        if (!sawToken) throw Error("Please give at least one day, e.g. mon,wed,fri or daily.");
        return result;
    }
} // namespace pesa
