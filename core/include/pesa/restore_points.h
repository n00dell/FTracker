// FTracker/core/include/pesa/restore_points.h
#pragma once

#include <pesa/common.h>
#include <pesa/date.h>

#include <optional>
#include <string>
#include <vector>

namespace pesa {
    struct RestorePoint {
        RestorePointId id = 0;
        std::string label; // "Before wiping 2026-11-21 to 2026-11-22"
        Date date; // when it was saved (the computer's local date)
        std::string time; // "18:40"
        Date from; // the days it can put back
        Date to;
        int entries = 0; // how many entries it holds
    };

    struct RestoreResult {
        std::string label;
        int entries = 0; // entries put back
        std::optional<RestorePointId> undoPoint; // going back saved its own restore point
    };

    // Created and owned by Pesa. Use it through app.restorePoints().
    // Restore points are saved by the bulk changes themselves (a past-dated rule, a skip, wipe all,
    // and restoring). Only the newest 5 are kept.
    class RestorePoints {
    public:
        explicit RestorePoints(detail::Database &db) noexcept : db_(db) {
        }

        [[nodiscard]] std::vector<RestorePoint> list() const; // newest first

        // Puts the saved days back as they were, and brings every rule and skip back to its saved
        // state (on, off or removed). A rule or skip created since is switched off. Entries you
        // added to those days since are lost, but a restore point is saved first, so this can be undone.
        RestoreResult restore(RestorePointId id);

    private:
        detail::Database &db_;
    };
} // namespace pesa
