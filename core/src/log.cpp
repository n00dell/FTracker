//
// Created by lee on 11/25/26.
//
// FTracker/core/src/log.cpp
#include <pesa/log.h>

#include "db.h"

#include <optional>

namespace pesa {
    namespace {
        // Which kind a tab shows. "All" has none: it shows every kind.
        std::optional<LogKind> kindOf(LogFilter filter) {
            switch (filter) {
                case LogFilter::All: return std::nullopt;
                case LogFilter::Auto: return LogKind::Rule;
                case LogFilter::Edits: return LogKind::Edit;
                case LogFilter::Savings: return LogKind::Savings;
                case LogFilter::Skipped: return LogKind::Skip;
                case LogFilter::Issues: return LogKind::Issue;
            }
            return std::nullopt;
        }
    } // namespace

    std::string logKindName(LogKind kind) {
        switch (kind) {
            case LogKind::Rule: return "Rule";
            case LogKind::Manual: return "Manual";
            case LogKind::Edit: return "Edit";
            case LogKind::Savings: return "Savings";
            case LogKind::Skip: return "Skipped";
            case LogKind::Issue: return "Issue";
            case LogKind::Pay: return "Pay";
        }
        return "";
    }

    std::vector<LogEvent> Log::list(LogFilter filter, int limit) const {
        if (limit < 1) throw Error("The limit must be at least 1.");
        const auto kind = kindOf(filter);

        // Newest first. The id only ever grows, so it is the order things happened in.
        std::string sql = "SELECT id, created_at, kind, message FROM log ";
        if (kind) sql += "WHERE kind = ? ";
        sql += "ORDER BY id DESC LIMIT ?";

        auto st = db_.prepare(sql);
        int next = 1;
        if (kind) st.bind(next++, static_cast<std::int64_t>(*kind));
        st.bind(next, static_cast<std::int64_t>(limit));

        std::vector<LogEvent> out;
        while (st.step()) {
            const std::string stamp = st.text(1); // "2026-11-24 18:40:12"
            out.push_back(LogEvent{
                .id = st.integer(0),
                .date = Date::parse(stamp.substr(0, 10)),
                .time = stamp.substr(11, 5),
                .kind = static_cast<LogKind>(st.integer(2)),
                .message = st.text(3),
            });
        }
        return out;
    }

    int Log::issueCount() const {
        auto st = db_.prepare("SELECT COUNT(*) FROM log WHERE kind = ?");
        st.bind(1, static_cast<std::int64_t>(LogKind::Issue));
        st.step();
        return static_cast<int>(st.integer(0));
    }
} // namespace pesa
