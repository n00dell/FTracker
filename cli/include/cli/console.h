// FTracker/cli/include/cli/console.h
// Everything that talks to the person: reading lines and asking questions.
// It works on any streams, so tests can feed it a script instead of a keyboard.
#pragma once

#include <pesa/common.h>
#include <pesa/date.h>

#include <exception>
#include <functional>
#include <iosfwd>
#include <optional>
#include <string>
#include <vector>

namespace cli {

// Typing q at any question, a blank answer with no default, or the end of the input: leave the
// command and go back to the menu (or the prompt).
class Cancelled : public std::exception {
public:
    const char* what() const noexcept override { return "Cancelled."; }
};

// Typing < at a question: go back one question. (See runSteps.)
class GoBack : public std::exception {
public:
    const char* what() const noexcept override { return "Went back."; }
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

    // Questions. Enter takes the [default]; with no default it cancels. q cancels, < goes back.
    // Wrong answers are explained and asked again.
    std::string      askText(const std::string& label, const std::optional<std::string>& def = std::nullopt);
    pesa::Money      askAmount(const std::string& label, std::optional<pesa::Money> def = std::nullopt);
    pesa::Date       askDate(const std::string& label, std::optional<pesa::Date> def = std::nullopt);
    pesa::WeekdaySet askDays(const std::string& label, pesa::WeekdaySet def);
    bool             askYesNo(const std::string& label, bool def);

    // Like askAmount / askDate, but a blank answer means "none" instead of cancelling.
    std::optional<pesa::Money> askOptionalAmount(const std::string& label);
    std::optional<pesa::Date>  askOptionalDate(const std::string& label);

    // Shows a numbered list and returns the 0-based index picked.
    std::size_t askChoice(const std::string& label, const std::vector<std::string>& options,
                          std::optional<std::size_t> def = std::nullopt);

private:
    std::string readAnswer(const std::string& prompt);   // handles q and <; may return ""
    std::string askRaw(const std::string& label, const std::optional<std::string>& def);

    std::istream& in_;
    std::ostream& out_;
    std::ostream& err_;
    bool          color_;
};

// One question of a guided command. `when` (optional) says whether it is asked at all.
struct Step {
    std::function<void()> ask;    // asks and stores the answer in a variable the command owns
    std::function<bool()> when;   // empty = always asked
};

// Walks through the questions in order. Typing < answers "go back": the previous question is asked
// again, with the earlier answer as its default. Going back from the first question leaves the command.
void runSteps(const std::vector<Step>& steps);

} // namespace cli