// FTracker/core/src/settings.cpp
#include <pesa/settings.h>

#include "db.h"
#include "util.h"

#include <charconv>

namespace pesa {

namespace {

constexpr const char* kSetupDone = "setup_done";
constexpr const char* kPay       = "expected_pay";
constexpr const char* kPayday    = "expected_payday";
constexpr const char* kAutoPost  = "auto_post_enabled";

} // namespace

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

// ---------------- first run and pay ----------------

bool Settings::setupDone() const { return get(kSetupDone) == "1"; }

void Settings::completeSetup(std::optional<Money> expectedPay, std::optional<Date> expectedPayday) {
    if (expectedPay) detail::requirePositive(*expectedPay);
    // A payday without a pay would be meaningless: projections need both.
    if (!expectedPay) expectedPayday.reset();

    detail::Transaction tx(db_);
    if (expectedPay) set(kPay, std::to_string(*expectedPay)); else erase(kPay);
    if (expectedPayday) set(kPayday, expectedPayday->toString()); else erase(kPayday);
    set(kSetupDone, "1");
    tx.commit();
}

std::optional<Money> Settings::expectedPay() const {
    const auto text = get(kPay);
    if (!text) return std::nullopt;
    Money value = 0;
    const auto [end, ec] = std::from_chars(text->data(), text->data() + text->size(), value);
    if (ec != std::errc{} || end != text->data() + text->size() || value <= 0) return std::nullopt;
    return value;
}

std::optional<Date> Settings::expectedPayday() const {
    const auto text = get(kPayday);
    if (!text) return std::nullopt;
    try {
        return Date::parse(*text);
    } catch (const Error&) {
        return std::nullopt;   // a damaged value counts as "not set" instead of crashing the app
    }
}

// ---------------- the daily job ----------------

bool Settings::autoPostEnabled() const { return get(kAutoPost) != "0"; }   // missing = on

void Settings::setAutoPostEnabled(bool enabled) { set(kAutoPost, enabled ? "1" : "0"); }

} // namespace pesa