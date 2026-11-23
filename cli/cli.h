// FTracker/cli/cli.h
#pragma once

#include <pesa/pesa.h>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace cli {

    using Args = std::vector<std::string>;

    void printUsage();

    int runCategory(pesa::Pesa& app, const Args& args);
    int runItem(pesa::Pesa& app, const Args& args);

    // ---- helpers ----
    std::optional<std::int64_t> parseInt(const std::string& text);        // whole numbers only
    std::string joinFrom(const Args& args, std::size_t from);             // words -> one string
    std::optional<std::string> takeOption(Args& args, const std::string& flag);  // "--days X"
    bool takeFlag(Args& args, const std::string& flag);                   // "--all"
    // "--all" -> IncludeInactive, "--deleted" -> DeletedOnly, neither -> ActiveOnly
    pesa::Visibility takeVisibility(Args& args);

} // namespace cli
