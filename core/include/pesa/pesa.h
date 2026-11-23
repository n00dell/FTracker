// FTracker/core/include/pesa/pesa.h
// The front door. It opens the database and hands out one object per feature.
// The CLI and GUI include this file only. Nothing here mentions SQLite.
#pragma once

#include <pesa/categories.h>
#include <pesa/common.h>
#include <pesa/items.h>

#include <filesystem>
#include <memory>

namespace pesa {

    // Where the database lives by default on this operating system.
    std::filesystem::path defaultDatabasePath();

    class Pesa {
    public:
        // Opens the file (creating it and its folder if needed) and upgrades its layout.
        explicit Pesa(const std::filesystem::path& dbFile);
        ~Pesa();
        Pesa(const Pesa&) = delete;
        Pesa& operator=(const Pesa&) = delete;

        Categories&       categories();
        const Categories& categories() const;
        Items&            items();
        const Items&      items() const;

        // Version of the database layout currently in the file.
        [[nodiscard]] int schemaVersion() const;

    private:
        struct Impl;                    // hidden: holds the sqlite connection and the features
        std::unique_ptr<Impl> impl_;
    };

} // namespace pesa
