// FTracker/core/include/pesa/items.h
#pragma once

#include <pesa/common.h>

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace pesa {
    struct Item {
        ItemId id = 0;
        CategoryId categoryId = 0;
        std::string categoryName; // filled in for convenience (read only)
        std::string name;
        Money defaultAmount = 0;
        bool isActive = true; // false = paused (name stays reserved)
        bool isDeleted = false; // true  = soft deleted (name is free to reuse)
    };

    struct NewItem {
        CategoryId categoryId = 0;
        std::string name;
        Money defaultAmount = 0;
    };

    // Created and owned by Pesa. Use it through app.items().
    class Items {
    public:
        explicit Items(detail::Database &db) noexcept : db_(db) {
        }

        ItemId add(const NewItem &item);

        void rename(ItemId id, std::string_view newName);

        void setDefaultAmount(ItemId id, Money amount);

        void setActive(ItemId id, bool active);

        void remove(ItemId id); // soft delete: hides it, keeps the row
        void restore(ItemId id); // undo a delete (its category must exist, its name be free)

        // Finds a row even if it is deleted, so history can still look it up.
        [[nodiscard]] std::optional<Item> find(ItemId id) const;

        // Items of a deleted or inactive category are hidden from ActiveOnly lists.
        [[nodiscard]] std::vector<Item> list(std::optional<CategoryId> category = std::nullopt,
                                             Visibility show = Visibility::ActiveOnly) const;

    private:
        detail::Database &db_;
    };
} // namespace pesa
