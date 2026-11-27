// FTracker/core/include/pesa/categories.h
#pragma once

#include <pesa/common.h>
#include <pesa/date.h>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace pesa {
    struct Category {
        CategoryId id = 0;
        std::string name;
        int sortOrder = 0;
        bool isActive = true; // false = paused (name stays reserved)
        bool isDeleted = false; // true  = soft deleted (name is free to reuse)
        bool isSystem = false; // true  = built in (Misc): cannot be renamed, paused or deleted
        std::optional<Date> expiresOn;      // the LAST day it is in use; it pauses itself after that
    };

    // Created and owned by Pesa. Use it through app.categories().
    class Categories {
    public:
        explicit Categories(detail::Database &db) noexcept : db_(db) {
        }

        CategoryId add(std::string_view name);

        void rename(CategoryId id, std::string_view newName);

        void setActive(CategoryId id, bool active);

        void remove(CategoryId id); // soft delete: hides it, keeps the row
        void restore(CategoryId id); // undo a delete (fails if the name is taken again)
        // A short-lived category: it pauses itself after this day (Misc cannot have one). Empty removes it.
        // Resuming a paused category (setActive) also removes its end date.
        void setExpiry(CategoryId id, std::optional<Date> lastDay);
        // On launch: pauses categories whose last day has passed. Returns their names.
        std::vector<std::string> pauseExpired(const Date& today);
        // The built-in "Misc" category: home for anything that fits nowhere else.
        [[nodiscard]] CategoryId misc() const;

        // Finds a row even if it is deleted, so history can still look it up.
        [[nodiscard]] std::optional<Category> find(CategoryId id) const;

        [[nodiscard]] std::vector<Category> list(Visibility show = Visibility::ActiveOnly) const;

    private:
        detail::Database &db_;
    };
} // namespace pesa
