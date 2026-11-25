// FTracker/core/include/pesa/pesa.h
// The front door. It opens the database and hands out one object per feature.
// The CLI and GUI include this file only. Nothing here mentions SQLite.
#pragma once

#include <pesa/categories.h>
#include <pesa/common.h>
#include <pesa/date.h>
#include <pesa/days.h>
#include <pesa/entries.h>
#include <pesa/items.h>
#include <pesa/rules.h>
#include <pesa/settings.h>
#include <pesa/daily_job.h>
#include <pesa/periods.h>
#include <pesa/savings.h>
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
        Entries&          entries();
        const Entries&    entries() const;
        Days&             days();
        const Days&       days() const;
        Rules&            rules();
        const Rules&      rules() const;
        Settings&         settings();
        const Settings&   settings() const;
        DailyJob&         dailyJob();
        const DailyJob&   dailyJob() const;
        Savings&          savings();
        const Savings&    savings() const;
        Periods&          periods();
        const Periods&    periods() const;

        // The front door for first-run setup: saves the settings AND opens the first period
        // (with the starting balance) in one step. Safe to run again: it never opens a second
        // period, it only updates the mode, pay and payday.
        void completeSetup(const SetupAnswers& answers, const Date& today);
        
        // Version of the database layout currently in the file.
        [[nodiscard]] int schemaVersion() const;

    private:
        struct Impl;                    // hidden: holds the sqlite connection and the features
        std::unique_ptr<Impl> impl_;
    };

} // namespace pesa
