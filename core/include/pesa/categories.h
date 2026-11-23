// FTracker/core/include/pesa/categories.h
#pragma once

#include <pesa/common.h>

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace pesa {

    struct Category {
        CategoryId  id = 0;
        std::string name;
        int         sortOrder = 0;
        bool        isActive = true;    // false = paused (name stays reserved)
        bool        isDeleted = false;  // true  = soft deleted (name is free to reuse)
    };

    // Created and owned by Pesa. Use it through app.categories().
    class Categories {
    public:
        explicit Categories(detail::Database& db) noexcept : db_(db) {}

        CategoryId add(std::string_view name);
        void       rename(CategoryId id, std::string_view newName);
        void       setActive(CategoryId id, bool active);
        void       remove(CategoryId id);    // soft delete: hides it, keeps the row
        void       restore(CategoryId id);   // undo a delete (fails if the name is taken again)

        // Finds a row even if it is deleted, so history can still look it up.
        [[nodiscard]] std::optional<Category> find(CategoryId id) const;
        [[nodiscard]] std::vector<Category> list(Visibility show = Visibility::ActiveOnly) const;

    private:
        detail::Database& db_;
    };

} // namespace pesa
