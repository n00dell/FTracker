// FTracker/gui/src/format.h
#pragma once

#include <cstdint>
#include <string>

// 42300 -> "42,300"
inline std::string formatMoney(std::int64_t v) {
    std::string digits = std::to_string(v < 0 ? -v : v);
    std::string out;
    int n = 0;
    for (auto it = digits.rbegin(); it != digits.rend(); ++it) {
        if (n && n % 3 == 0) out.insert(out.begin(), ',');
        out.insert(out.begin(), *it);
        ++n;
    }
    return v < 0 ? "−" + out : out;
}