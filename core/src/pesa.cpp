// FTracker/core/src/pesa.cpp
#include <pesa/pesa.h>

#include "db.h"
#include "migrations.h"

namespace pesa {

    // Order matters: `db` must exist before the features that point at it.
    struct Pesa::Impl {
        detail::Database db;
        Categories       categories;
        Items            items;
        Entries          entries;
        Days             days;
        Rules            rules;
        Settings         settings;

        explicit Impl(const std::filesystem::path& file)
            : db(file), categories(db), items(db), entries(db), days(db), rules(db), settings(db) {}
    };

    Pesa::Pesa(const std::filesystem::path& dbFile) : impl_(std::make_unique<Impl>(dbFile)) {
        detail::migrate(impl_->db);
    }

    Pesa::~Pesa() = default;   // defined here because Impl is complete only in this file

    Categories&       Pesa::categories()       { return impl_->categories; }
    const Categories& Pesa::categories() const { return impl_->categories; }
    Items&            Pesa::items()            { return impl_->items; }
    const Items&      Pesa::items() const      { return impl_->items; }
    Entries&          Pesa::entries()          { return impl_->entries; }
    const Entries&    Pesa::entries() const    { return impl_->entries; }
    Days&             Pesa::days()             { return impl_->days; }
    const Days&       Pesa::days() const       { return impl_->days; }
    Rules&            Pesa::rules()            { return impl_->rules; }
    const Rules&      Pesa::rules() const      { return impl_->rules; }
    Settings&         Pesa::settings()         { return impl_->settings; }
    const Settings&   Pesa::settings() const   { return impl_->settings; }

    int Pesa::schemaVersion() const { return detail::currentSchemaVersion(impl_->db); }

} // namespace pesa
