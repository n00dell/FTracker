// FTracker/cli/util.cpp
#include "cli.h"

#include <algorithm>
#include <charconv>
#include <iostream>

namespace cli {

void printUsage() {
    std::cout <<
        "Usage: pesa-cli [--db <file>] <command>\n\n"
        "Categories\n"
        "  category add <name>\n"
        "  category list [--all | --deleted]\n"
        "  category rename <id> <new name>\n"
        "  category deactivate <id>     pause it (name stays reserved)\n"
        "  category activate <id>\n"
        "  category delete <id>         soft delete (name becomes free)\n"
        "  category restore <id>        undo a delete\n\n"
        "Items  (<category> is an id or a name)\n"
        "  item add <category> <amount> <name> [--days mon,tue,.. | daily | weekdays | weekend | none]\n"
        "  item list [<category>] [--all | --deleted]\n"
        "  item rename <id> <new name>\n"
        "  item amount <id> <amount>\n"
        "  item days <id> <days>\n"
        "  item deactivate <id>\n"
        "  item activate <id>\n"
        "  item delete <id>\n"
        "  item restore <id>\n";
}

std::optional<std::int64_t> parseInt(const std::string& text) {
    std::int64_t value = 0;
    const auto [end, ec] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (ec != std::errc{} || end != text.data() + text.size()) return std::nullopt;
    return value;
}

std::string joinFrom(const Args& args, std::size_t from) {
    std::string out;
    for (std::size_t i = from; i < args.size(); ++i) {
        if (i > from) out += ' ';
        out += args[i];
    }
    return out;
}

std::optional<std::string> takeOption(Args& args, const std::string& flag) {
    const auto it = std::ranges::find(args, flag);
    if (it == args.end()) return std::nullopt;
    if (it + 1 == args.end()) throw pesa::Error("Missing value after " + flag + ".");
    std::string value = *(it + 1);
    args.erase(it, it + 2);
    return value;
}

bool takeFlag(Args& args, const std::string& flag) {
    const auto it = std::ranges::find(args, flag);
    if (it == args.end()) return false;
    args.erase(it);
    return true;
}

pesa::Visibility takeVisibility(Args& args) {
    const bool all = takeFlag(args, "--all");
    const bool deleted = takeFlag(args, "--deleted");
    if (all && deleted) throw pesa::Error("Use either --all or --deleted, not both.");
    if (deleted) return pesa::Visibility::DeletedOnly;
    if (all)     return pesa::Visibility::IncludeInactive;
    return pesa::Visibility::ActiveOnly;
}

} // namespace cli
