// FTracker/core/src/util.h  (PRIVATE)
#pragma once

#include <pesa/common.h>

#include <cctype>
#include <string>
#include <string_view>

namespace pesa::detail {

    // Trim spaces at both ends; throw `emptyMessage` if nothing is left.
    inline std::string cleanName(std::string_view raw, const char* emptyMessage) {
        auto isSpace = [](unsigned char c) { return std::isspace(c) != 0; };
        while (!raw.empty() && isSpace(static_cast<unsigned char>(raw.front()))) raw.remove_prefix(1);
        while (!raw.empty() && isSpace(static_cast<unsigned char>(raw.back())))  raw.remove_suffix(1);
        if (raw.empty()) throw Error(emptyMessage);
        return std::string(raw);
    }

} // namespace pesa::detail
