// FTracker/core/include/pesa/log.h
#pragma once

#include <pesa/common.h>
#include <pesa/date.h>

#include <string>
#include <vector>

namespace pesa {

    // The numbers are stored in the file, so never renumber them.
    enum class LogKind : int {
        Rule    = 0,   // posted by the daily job
        Manual  = 1,   // you added or deleted an entry
        Edit    = 2,   // an amount changed
        Savings = 3,   // money moved in or out of a pot
        Skip    = 4,   // a skip rule removed entries (skip rules come later)
        Issue   = 5,   // a problem, or the clock guard
        Pay     = 6,   // income, "I got paid", delayed pay
    };

    // The tabs of the Log screen. "All" shows every kind.
    enum class LogFilter { All, Auto, Edits, Savings, Skipped, Issues };

    [[nodiscard]] std::string logKindName(LogKind kind);   // "Rule", "Manual", "Edit", ...

    struct LogEvent {
        LogId       id = 0;
        Date        date;      // the day it happened (the computer's local date)
        std::string time;      // "18:40"
        LogKind     kind = LogKind::Manual;
        std::string message;
    };

    // Created and owned by Pesa. Use it through app.log().
    // This class only READS. Lines are written by the features themselves, inside the same
    // transaction as the change, so the log can never describe something that did not happen.
    class Log {
    public:
        explicit Log(detail::Database& db) noexcept : db_(db) {}

        // Newest first. `limit` must be at least 1.
        [[nodiscard]] std::vector<LogEvent> list(LogFilter filter = LogFilter::All, int limit = 100) const;
        [[nodiscard]] int issueCount() const;

    private:
        detail::Database& db_;
    };

} // namespace pesa