// FTracker/core/include/pesa/skips.h
#pragma once

#include <pesa/common.h>
#include <pesa/corrections.h>
#include <pesa/date.h>

#include <optional>
#include <string>
#include <vector>

namespace pesa {
    // What a skip covers: a whole category OR one item (exactly one of the two is set).
    struct SkipTarget {
        std::optional<CategoryId> categoryId;
        std::optional<ItemId> itemId;
        std::string name; // the category's or the item's name
    };

    struct Skip {
        SkipId id = 0;
        std::string name; // "Public holiday"
        Date from; // first and last day, both included
        Date to;
        std::vector<SkipTarget> targets;
        bool isDeleted = false;
    };

    struct NewSkip {
        std::string name;
        Date from;
        Date to;
        std::vector<CategoryId> categories;
        std::vector<ItemId> items;
    };

    // Created and owned by Pesa. Use it through app.skips().
    //
    // A skip runs BEFORE charge rules: for each covered item and day, the daily job leaves a grey
    // "Skipped" row instead of a normal entry. Adding a skip that reaches into days already
    // posted turns their untouched auto entries grey too (entries you edited or added by hand
    // stay). Removing a skip turns its grey rows back into normal entries.
    class Skips {
    public:
        explicit Skips(detail::Database &db) noexcept : db_(db) {
        }

        // Call with Mode::Preview first and show the person what would change.
        CorrectionResult add(const NewSkip &skip, const Date &today, Mode mode = Mode::Apply);

        CorrectionResult remove(SkipId id, Mode mode = Mode::Apply);

        [[nodiscard]] std::optional<Skip> find(SkipId id) const; // even if removed
        [[nodiscard]] std::vector<Skip> list(bool includeRemoved = false) const;

    private:
        detail::Database &db_;
    };
} // namespace pesa
