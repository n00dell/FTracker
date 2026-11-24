// FTracker/cli/include/cli/console.h
// Everything that talks to the person: reading lines and asking questions.
// It works on any streams, so tests can feed it a script instead of a keyboard.
#pragma once

#include <pesa/common.h>
#include <pesa/date.h>

#include <exception>
#include <iosfwd>
#include <optional>
#include <string>
#include <vector>

namespace cli {

// Thrown when the person backs out of a question (blank answer, or end of input).
class Cancelled : public std::exception {
public:
    const char* what() const noexcept override { return "Cancelled."; }
};

// Colour only when writing to a real terminal and NO_COLOR is not set.
bool stdoutSupportsColor();

class Console {
public:
    Console(std::istream& in, std::ostream& out, std::ostream& err, bool color);

    std::ostream& out() { return out_; }
    std::ostream& err() { return err_; }
    [[nodiscard]] bool color() const { return color_; }

    // Shows the prompt and reads a line. Empty optional = the input ended.
    std::optional<std::string> readLine(const std::string& prompt);

    // Questions. Pressing Enter takes the [default]; with no default it cancels.
    // Wrong answers are explained and asked again.
    std::string      askText(const std::string& label, const std::optional<std::string>& def = std::nullopt);
    pesa::Money      askAmount(const std::string& label, std::optional<pesa::Money> def = std::nullopt);
    std::optional<pesa::Money> askOptionalAmount(const std::string& label);
    pesa::Date       askDate(const std::string& label, std::optional<pesa::Date> def = std::nullopt);
    pesa::WeekdaySet askDays(const std::string& label, pesa::WeekdaySet def);
    bool             askYesNo(const std::string& label, bool def);

    // Shows a numbered list and returns the 0-based index picked ("q" cancels).
    std::size_t askChoice(const std::string& label, const std::vector<std::string>& options,
                          std::optional<std::size_t> def = std::nullopt);

private:
    std::string askRaw(const std::string& label, const std::optional<std::string>& def);

    std::istream& in_;
    std::ostream& out_;
    std::ostream& err_;
    bool          color_;
};

} // namespace cli
