// FTracker/core/src/pesa.cpp
#include <pesa/pesa.h>

#include "db.h"
#include "migrations.h"

namespace pesa {
    // Order matters: `db` must exist before the features that point at it.
    struct Pesa::Impl {
        detail::Database db;
        Categories categories;
        Items items;
        Entries entries;
        Days days;
        Rules rules;
        Settings settings;
        Savings savings;
        Periods periods;
        Log log;
        Skips skips;
        RestorePoints restorePoints;
        Goals goals;
        DailyJob dailyJob;

        explicit Impl(const std::filesystem::path &file)
            : db(file), categories(db), items(db), entries(db), days(db), rules(db),
              settings(db), dailyJob(db, settings), savings(db), periods(db, settings), log(db), skips(db),
              restorePoints(db), goals(db, settings) {
        }
    };

    Pesa::Pesa(const std::filesystem::path &dbFile) : impl_(std::make_unique<Impl>(dbFile)) {
        detail::migrate(impl_->db);
    }

    Pesa::~Pesa() = default; // defined here because Impl is complete only in this file

    Categories &Pesa::categories() { return impl_->categories; }
    const Categories &Pesa::categories() const { return impl_->categories; }
    Items &Pesa::items() { return impl_->items; }
    const Items &Pesa::items() const { return impl_->items; }
    Entries &Pesa::entries() { return impl_->entries; }
    const Entries &Pesa::entries() const { return impl_->entries; }
    Days &Pesa::days() { return impl_->days; }
    const Days &Pesa::days() const { return impl_->days; }
    Rules &Pesa::rules() { return impl_->rules; }
    const Rules &Pesa::rules() const { return impl_->rules; }
    Settings &Pesa::settings() { return impl_->settings; }
    const Settings &Pesa::settings() const { return impl_->settings; }
    Savings &Pesa::savings() { return impl_->savings; }
    const Savings &Pesa::savings() const { return impl_->savings; }
    Periods &Pesa::periods() { return impl_->periods; }
    const Periods &Pesa::periods() const { return impl_->periods; }
    Log &Pesa::log() { return impl_->log; }
    const Log &Pesa::log() const { return impl_->log; }
    Skips &Pesa::skips() { return impl_->skips; }
    const Skips &Pesa::skips() const { return impl_->skips; }
    RestorePoints &Pesa::restorePoints() { return impl_->restorePoints; }
    const RestorePoints &Pesa::restorePoints() const { return impl_->restorePoints; }
    Goals&            Pesa::goals()            { return impl_->goals; }
    const Goals&      Pesa::goals() const      { return impl_->goals; }
    DailyJob &Pesa::dailyJob() { return impl_->dailyJob; }
    const DailyJob &Pesa::dailyJob() const { return impl_->dailyJob; }

    void Pesa::completeSetup(const SetupAnswers &answers, const Date &today) {
        const bool regular = answers.mode == IncomeMode::Regular;
        if (regular && answers.payday && *answers.payday <= today) {
            throw Error("The payday must be after today.");
        }
        detail::Transaction tx(impl_->db);
        impl_->settings.completeSetup(answers);
        impl_->periods.ensureOpen(today, answers.startingBalance);
        impl_->periods.setExpectedPayday(regular ? answers.payday : std::nullopt);
        tx.commit();
    }

    int Pesa::schemaVersion() const { return detail::currentSchemaVersion(impl_->db); }
} // namespace pesa
