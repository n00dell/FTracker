// FTracker/core/include/pesa/corrections.h
// The shared vocabulary of bulk changes: things that reach back into days already posted.
#pragma once

#include <pesa/common.h>
#include <pesa/date.h>

#include <optional>
#include <string>
#include <vector>

namespace pesa {
    // Preview = work out what WOULD change, then undo all of it. Apply = do it.
    enum class Mode { Preview, Apply };

    struct ChangedEntry {
        Date date;
        std::string name;
        Money before = 0;
        Money after = 0; // 0 = it no longer counts (skipped or deleted)
    };

    struct CorrectionPreview {
        int entries = 0; // entries that change
        int days = 0; // days they are spread over
        Money before = 0; // their total now
        Money after = 0; // their total afterwards
        int keptEdited = 0; // entries you edited by hand: left alone
        int keptAdded = 0; // entries you added by hand: left alone
        std::vector<ChangedEntry> sample; // the first few, to show
    };

    struct CorrectionResult {
        CorrectionPreview changes;
        std::optional<RestorePointId> restorePoint; // set when applied and the past was touched
        std::int64_t created = 0; // id of the new rule or skip (0 in a preview)
    };
} // namespace pesa