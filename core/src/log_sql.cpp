//
// Created by lee on 11/25/26.
//
// FTracker/core/src/log_sql.cpp
#include "log_sql.h"

namespace pesa::detail {
    void writeLog(Database &db, LogKind kind, std::string_view message) {
        // created_at is not listed: the table fills it in with the current local time.
        db.prepare("INSERT INTO log(kind, message) VALUES (?, ?)")
                .bind(1, static_cast<std::int64_t>(kind))
                .bind(2, message)
                .run();
    }

    void writeLogOnce(Database &db, LogKind kind, std::string_view message) {
        {
            auto st = db.prepare("SELECT message FROM log WHERE kind = ? ORDER BY id DESC LIMIT 1");
            st.bind(1, static_cast<std::int64_t>(kind));
            if (st.step() && st.text(0) == message) return;
        } // the statement is finished here, before we write
        writeLog(db, kind, message);
    }
} // namespace pesa::detail
