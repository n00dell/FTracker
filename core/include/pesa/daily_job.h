// FTracker/core/include/pesa/daily_job.h
#pragma once

#include <pesa/common.h>
#include <pesa/date.h>
#include <pesa/settings.h>

#include <optional>
#include <string>

namespace pesa {

    // What one run of the job did, so the CLI and GUI can tell the person.
    struct DailyJobResult {
        bool                ran = false;          // false: auto-post is off, or the clock guard stopped it
        bool                clockBehind = false;  // the computer's date is before the last posted day
        int                 days = 0;             // days handled this run (a day with no entries counts)
        int                 entries = 0;          // entries posted this run
        Money               total = 0;            // their sum
        std::optional<Date> from;                 // first and last day handled
        std::optional<Date> to;
        std::string         problem;              // empty = everything went fine
    };

    // Posts the routine entries (charge rules) for every day that is due.
    // Created and owned by Pesa. Use it through app.dailyJob().
    //
    // The core never reads the clock itself: the caller hands over today's date and the time,
    // so tests can pretend it is any moment they like.
    class DailyJob {
    public:
        DailyJob(detail::Database& db, const Settings& settings) noexcept : db_(db), settings_(settings) {}

        // Safe to call any number of times: a day that is already posted is never posted again.
        DailyJobResult run(const Date& today, TimeOfDay now);

        // The last day the job finished, or empty if it has never run.
        [[nodiscard]] std::optional<Date> lastPosted() const;

    private:
        void postDay(const Date& date, DailyJobResult& result);

        detail::Database& db_;
        const Settings&   settings_;
    };

} // namespace pesa