// FTracker/core/src/settings.cpp
#include <pesa/settings.h>

#include "db.h"
#include "util.h"

#include <charconv>
#include <cstdio>
#include <ctime>

namespace pesa {
    namespace {
        constexpr const char *kSetupDone = "setup_done";
        constexpr const char *kPay = "expected_pay";
        constexpr const char *kMode = "income_mode";
        constexpr const char *kAutoPost = "auto_post_enabled";
        constexpr const char *kPostAfter = "post_after";
        constexpr const char *kAutoPay = "auto_pay_enabled";
        constexpr const char* kAutoCover  = "auto_cover_enabled";
        constexpr const char* kShortfall  = "shortfall_policy";
        constexpr TimeOfDay kDefaultPostAfter{20, 0};
    } // namespace

    // ---------------- TimeOfDay ----------------

    TimeOfDay TimeOfDay::parse(std::string_view text) {
        auto bad = [&] { return Error("A time must look like 08:30, got '" + std::string(text) + "'."); };

        const auto colon = text.find(':');
        if (colon == std::string_view::npos) throw bad();

        auto number = [&](std::string_view part) {
            int value = 0;
            const auto [end, ec] = std::from_chars(part.data(), part.data() + part.size(), value);
            if (part.empty() || ec != std::errc{} || end != part.data() + part.size()) throw bad();
            return value;
        };
        const int hour = number(text.substr(0, colon));
        const int minute = number(text.substr(colon + 1));
        if (hour < 0 || hour > 23 || minute < 0 || minute > 59) throw bad();
        return TimeOfDay{hour, minute};
    }

    TimeOfDay TimeOfDay::now() {
        const std::time_t t = std::time(nullptr);
        std::tm local{};
#if defined(_WIN32)
        localtime_s(&local, &t);
#else
        localtime_r(&t, &local);
#endif
        return TimeOfDay{local.tm_hour, local.tm_min};
    }

    std::string TimeOfDay::toString() const {
        char out[16];
        std::snprintf(out, sizeof out, "%02d:%02d", hour, minute);
        return out;
    }

    // ---------------- the key/value store ----------------

    std::optional<std::string> Settings::get(std::string_view key) const {
        auto st = db_.prepare("SELECT value FROM settings WHERE key = ?");
        st.bind(1, key);
        if (!st.step()) return std::nullopt;
        return st.text(0);
    }

    void Settings::set(std::string_view key, std::string_view value) {
        // "Upsert": insert the key, or if it already exists just replace its value.
        db_.prepare("INSERT INTO settings(key, value) VALUES (?, ?) "
                    "ON CONFLICT(key) DO UPDATE SET value = excluded.value")
                .bind(1, key).bind(2, value).run();
    }

    void Settings::erase(std::string_view key) {
        db_.prepare("DELETE FROM settings WHERE key = ?").bind(1, key).run();
    }

    // ---------------- first run, mode and pay ----------------

    bool Settings::setupDone() const { return get(kSetupDone) == "1"; }

    void Settings::completeSetup(const SetupAnswers &answers) {
        const bool regular = answers.mode == IncomeMode::Regular;
        if (regular) {
            if (!answers.pay || !answers.payday) throw Error("A regular income needs a pay and a payday.");
            detail::requirePositive(*answers.pay);
        }

        detail::Transaction tx(db_);
        set(kMode, regular ? "regular" : "irregular");
        if (regular) set(kPay, std::to_string(*answers.pay));
        else erase(kPay);
        set(kSetupDone, "1");
        tx.commit();
    }

    IncomeMode Settings::incomeMode() const {
        const auto stored = get(kMode);
        if (stored == "regular") return IncomeMode::Regular;
        if (stored == "irregular") return IncomeMode::Irregular;
        return expectedPay() ? IncomeMode::Regular : IncomeMode::Irregular; // a file from before this setting
    }

    std::optional<Money> Settings::expectedPay() const {
        const auto text = get(kPay);
        if (!text) return std::nullopt;
        Money value = 0;
        const auto [end, ec] = std::from_chars(text->data(), text->data() + text->size(), value);
        if (ec != std::errc{} || end != text->data() + text->size() || value <= 0) return std::nullopt;
        return value;
    }

    // ---------------- the daily job and getting paid ----------------

    bool Settings::autoPostEnabled() const { return get(kAutoPost) != "0"; } // missing = on
    void Settings::setAutoPostEnabled(bool enabled) { set(kAutoPost, enabled ? "1" : "0"); }

    TimeOfDay Settings::postAfter() const {
        const auto text = get(kPostAfter);
        if (!text) return kDefaultPostAfter;
        try {
            return TimeOfDay::parse(*text);
        } catch (const Error &) {
            return kDefaultPostAfter;
        }
    }

    void Settings::setPostAfter(TimeOfDay time) { set(kPostAfter, time.toString()); }

    bool Settings::autoPayEnabled() const { return get(kAutoPay) != "0"; } // missing = on
    void Settings::setAutoPayEnabled(bool enabled) { set(kAutoPay, enabled ? "1" : "0"); }
    // ---------------- when earned runs out ----------------

    bool Settings::autoCoverEnabled() const { return get(kAutoCover) == "1"; }   // missing = off
    void Settings::setAutoCoverEnabled(bool enabled) { set(kAutoCover, enabled ? "1" : "0"); }

    ShortfallPolicy Settings::shortfallPolicy() const {
        const auto stored = get(kShortfall);
        if (stored == "savings") return ShortfallPolicy::TakeFromSavings;
        if (stored == "carry")   return ShortfallPolicy::CarryForward;
        return ShortfallPolicy::Record;
    }

    void Settings::setShortfallPolicy(ShortfallPolicy policy) {
        set(kShortfall, policy == ShortfallPolicy::TakeFromSavings ? "savings"
                      : policy == ShortfallPolicy::CarryForward    ? "carry" : "record");
    }
} // namespace pesa