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

        explicit Impl(const std::filesystem::path& file) : db(file), categories(db), items(db) {}
    };

    Pesa::Pesa(const std::filesystem::path& dbFile) : impl_(std::make_unique<Impl>(dbFile)) {
        detail::migrate(impl_->db);
    }

    Pesa::~Pesa() = default;   // defined here because Impl is complete only in this file

    Categories&       Pesa::categories()       { return impl_->categories; }
    const Categories& Pesa::categories() const { return impl_->categories; }
    Items&            Pesa::items()            { return impl_->items; }
    const Items&      Pesa::items() const      { return impl_->items; }

    int Pesa::schemaVersion() const { return detail::currentSchemaVersion(impl_->db); }

} // namespace pesa
