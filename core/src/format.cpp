//
// Created by lee on 11/25/26.
//
// FTracker/core/src/format.cpp
#include <pesa/common.h>

namespace pesa {

    std::string formatMoney(Money amount) {
        const bool negative = amount < 0;
        const std::string digits = std::to_string(negative ? -amount : amount);
        std::string out;
        for (std::size_t i = 0; i < digits.size(); ++i) {
            if (i != 0 && (digits.size() - i) % 3 == 0) out += ',';
            out += digits[i];
        }
        return negative ? "-" + out : out;
    }

} // namespace pesa