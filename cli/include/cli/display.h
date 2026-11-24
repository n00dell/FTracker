// FTracker/cli/include/cli/display.h
// Turning data into readable tables.
#pragma once

#include <cli/console.h>

#include <pesa/pesa.h>

#include <string>
#include <vector>

namespace cli {

    enum class Style { Normal, Bold, Dim, Yellow, Blue };

    // A text table whose columns size themselves to the content.
    class Table {
    public:
        enum class Align { Left, Right };
        struct Column {
            std::string title;
            Align       align = Align::Left;
            std::size_t maxWidth = 36;   // longer cells are cut with "..."
        };

        explicit Table(std::vector<Column> columns);

        void addRow(std::vector<std::string> cells, Style style = Style::Normal);
        void addSection(std::string title);   // a heading line between groups of rows

        [[nodiscard]] std::string render(bool color) const;
        void print(Console& io) const;

    private:
        struct Row {
            bool                     isSection = false;
            std::vector<std::string> cells;     // for a section: cells[0] is the title
            Style                    style = Style::Normal;
        };
        std::vector<Column> columns_;
        std::vector<Row>    rows_;
    };

    std::string weekdayName(pesa::Weekday day);              // "Monday"
    std::string entryStateText(const pesa::Entry& entry);    // "auto", "edited (was 50)", ...

    void printDay(Console& io, const pesa::DayView& day);
    void printCategories(Console& io, const std::vector<pesa::Category>& categories);
    void printItems(Console& io, const std::vector<pesa::Item>& items);
    void printRules(Console& io, const std::vector<pesa::Rule>& rules);
    void printCatalog(Console& io, const std::vector<pesa::Category>& categories,
                      const std::vector<pesa::Item>& items);

} // namespace cli
