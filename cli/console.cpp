// FTracker/cli/console.cpp
#include <cli/console.h>

#include <cli/text.h>

#include <cctype>
#include <cstdlib>
#include <istream>
#include <ostream>
#include <string_view>

#if !defined(_WIN32)
#include <unistd.h>
#endif

namespace cli {

bool stdoutSupportsColor() {
#if defined(_WIN32)
    return false;   // plain text on Windows for now
#else
    const char* noColor = std::getenv("NO_COLOR");
    const char* term = std::getenv("TERM");
    if (noColor != nullptr && *noColor != '\0') return false;
    if (term != nullptr && std::string_view(term) == "dumb") return false;
    return isatty(STDOUT_FILENO) != 0;
#endif
}

Console::Console(std::istream& in, std::ostream& out, std::ostream& err, bool color)
    : in_(in), out_(out), err_(err), color_(color) {}

std::optional<std::string> Console::readLine(const std::string& prompt) {
    out_ << prompt << std::flush;
    std::string line;
    if (!std::getline(in_, line)) return std::nullopt;

    std::size_t start = 0;
    while (start < line.size() && std::isspace(static_cast<unsigned char>(line[start]))) ++start;
    std::size_t end = line.size();
    while (end > start && std::isspace(static_cast<unsigned char>(line[end - 1]))) --end;
    return line.substr(start, end - start);
}

std::string Console::askRaw(const std::string& label, const std::optional<std::string>& def) {
    std::string prompt = label;
    if (def) prompt += " [" + *def + "]";
    prompt += ": ";

    const auto line = readLine(prompt);
    if (!line) {                       // the input ended (Ctrl-D, or a script ran out)
        out_ << "\n";
        throw Cancelled{};
    }
    if (line->empty()) {
        if (def) return *def;          // Enter = take the default
        throw Cancelled{};             // Enter with no default = back out
    }
    return *line;
}

std::string Console::askText(const std::string& label, const std::optional<std::string>& def) {
    return askRaw(label, def);
}

pesa::Money Console::askAmount(const std::string& label, std::optional<pesa::Money> def) {
    while (true) {
        const std::string text = askRaw(label, def ? std::optional<std::string>(formatMoney(*def))
                                                   : std::nullopt);
        try {
            const pesa::Money amount = parseAmount(text);
            if (amount <= 0) throw pesa::Error("The amount must be more than 0.");
            return amount;
        } catch (const pesa::Error& e) {
            out_ << "  " << e.what() << "\n";
        }
    }
}
    std::optional<pesa::Money> Console::askOptionalAmount(const std::string& label) {
    while (true) {
        const auto line = readLine(label + " (Enter to skip): ");
        if (!line) {                       // the input ended: that is a real cancel
            out_ << "\n";
            throw Cancelled{};
        }
        if (line->empty()) return std::nullopt;
        try {
            const pesa::Money amount = parseAmount(*line);
            if (amount <= 0) throw pesa::Error("The amount must be more than 0.");
            return amount;
        } catch (const pesa::Error& e) {
            out_ << "  " << e.what() << "\n";
        }
    }
}
pesa::Date Console::askDate(const std::string& label, std::optional<pesa::Date> def) {
    while (true) {
        const std::string text = askRaw(label + " (today, tomorrow, 2026-11-23)",
                                        def ? std::optional<std::string>(def->toString()) : std::nullopt);
        try {
            return parseDate(text);
        } catch (const pesa::Error& e) {
            out_ << "  " << e.what() << "\n";
        }
    }
}

pesa::WeekdaySet Console::askDays(const std::string& label, pesa::WeekdaySet def) {
    while (true) {
        const std::string text = askRaw(label + " (mon,wed,.. / daily / weekdays / weekend / none)",
                                        daysInputText(def));
        try {
            return pesa::WeekdaySet::parse(text);
        } catch (const pesa::Error& e) {
            out_ << "  " << e.what() << "\n";
        }
    }
}

bool Console::askYesNo(const std::string& label, bool def) {
    while (true) {
        const std::string text = toLower(askRaw(label + " (y/n)", std::string(def ? "y" : "n")));
        if (text == "y" || text == "yes") return true;
        if (text == "n" || text == "no")  return false;
        out_ << "  Please answer y or n.\n";
    }
}

std::size_t Console::askChoice(const std::string& label, const std::vector<std::string>& options,
                               std::optional<std::size_t> def) {
    if (options.empty()) throw pesa::Error("There is nothing to choose from.");

    out_ << label << ":\n";
    for (std::size_t i = 0; i < options.size(); ++i) {
        out_ << "  " << (i + 1) << ") " << options[i] << "\n";
    }
    while (true) {
        const std::string text = askRaw("Choose a number (q to cancel)",
                                        def ? std::optional<std::string>(std::to_string(*def + 1))
                                            : std::nullopt);
        if (toLower(text) == "q") throw Cancelled{};
        const auto number = parseInt(text);
        if (number && *number >= 1 && *number <= static_cast<std::int64_t>(options.size())) {
            return static_cast<std::size_t>(*number - 1);
        }
        out_ << "  Please enter a number from 1 to " << options.size() << ".\n";
    }
}

} // namespace cli
