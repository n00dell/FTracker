// FTracker/core/src/migrations.h
// FTracker/core/src/migrations.h  (PRIVATE)
#pragma once
#include "db.h"

namespace pesa::detail {
    int latestSchemaVersion();

    int currentSchemaVersion(Database & db);
    void migrate(Database & db); // bring the file up to the latest layout
} // namespace pesa::detail
