// FTracker/core/src/log_sql.h
// (PRIVATE) How the features write log lines.
#pragma once

#include <pesa/log.h>

#include "db.h"

#include <string_view>

namespace pesa::detail {
    // Adds one line. Call it inside the same Transaction as the change it describes.
    void writeLog(Database &db, LogKind kind, std::string_view message);

    // Same, but does nothing if the newest line of that kind already says exactly this.
    // For problems that would otherwise repeat on every launch.
    void writeLogOnce(Database &db, LogKind kind, std::string_view message);
} // namespace pesa::detail
