// FTracker/cli/display.cpp
#include <cli/display.h>
#include <iomanip>
#include <cli/text.h>

#include <array>
#include <ostream>

namespace cli {
    namespace {
        std::string paint(const std::string &text, Style style, bool color) {
            if (!color || style == Style::Normal || text.empty()) return text;
            const char *code = "";
            switch (style) {
                case Style::Bold: code = "\x1b[1m";
                    break;
                case Style::Dim: code = "\x1b[2m";
                    break;
                case Style::Yellow: code = "\x1b[33m";
                    break;
                case Style::Blue: code = "\x1b[34m";
                    break;
                case Style::Normal: break;
            }
            return code + text + "\x1b[0m";
        }

        // Words that stand next to a name when it is not in normal use.
        std::string statusWord(bool isActive, bool isDeleted, bool isSystem = false) {
            if (isDeleted) return "deleted";
            if (!isActive) return "paused";
            if (isSystem) return "built in";
            return "";
        }

        // Only names that are switched off look faded; "built in" is normal.
        Style statusStyle(bool isActive, bool isDeleted) {
            return (isActive && !isDeleted) ? Style::Normal : Style::Dim;
        }
    } // namespace

    // ---------------- Table ----------------

    Table::Table(std::vector<Column> columns) : columns_(std::move(columns)) {
    }

    void Table::addRow(std::vector<std::string> cells, Style style) {
        cells.resize(columns_.size());
        for (std::size_t i = 0; i < cells.size(); ++i) cells[i] = truncate(cells[i], columns_[i].maxWidth);
        rows_.push_back(Row{.isSection = false, .cells = std::move(cells), .style = style});
    }

    void Table::addSection(std::string title) {
        rows_.push_back(Row{.isSection = true, .cells = {std::move(title)}, .style = Style::Bold});
    }

    std::string Table::render(bool color) const {
        std::vector<std::size_t> widths(columns_.size());
        for (std::size_t c = 0; c < columns_.size(); ++c) widths[c] = displayWidth(columns_[c].title);
        for (const Row &row: rows_) {
            if (row.isSection) continue;
            for (std::size_t c = 0; c < columns_.size(); ++c)
                widths[c] = std::max(
                    widths[c], displayWidth(row.cells[c]));
        }

        auto line = [&](const std::vector<std::string> &cells) {
            std::string s = "  ";
            for (std::size_t c = 0; c < columns_.size(); ++c) {
                const std::string pad(widths[c] - displayWidth(cells[c]), ' ');
                s += columns_[c].align == Align::Right ? pad + cells[c] : cells[c] + pad;
                if (c + 1 < columns_.size()) s += "  ";
            }
            while (!s.empty() && s.back() == ' ') s.pop_back();
            return s;
        };

        std::vector<std::string> titles;
        std::vector<std::string> rules;
        for (std::size_t c = 0; c < columns_.size(); ++c) {
            titles.push_back(columns_[c].title);
            rules.emplace_back(widths[c], '-');
        }

        std::string out = paint(line(titles), Style::Bold, color) + "\n" + line(rules) + "\n";
        bool firstRow = true;
        for (const Row &row: rows_) {
            if (row.isSection) {
                out += std::string(firstRow ? "" : "\n") + paint(row.cells[0], row.style, color) + "\n";
            } else {
                out += paint(line(row.cells), row.style, color) + "\n";
            }
            firstRow = false;
        }
        return out;
    }

    void Table::print(Console &io) const { io.out() << render(io.color()); }

    // ---------------- helpers for entries ----------------

    std::string weekdayName(pesa::Weekday day) {
        static constexpr std::array<const char *, 7> names = {
            "Monday", "Tuesday", "Wednesday", "Thursday",
            "Friday", "Saturday", "Sunday"
        };
        return names[static_cast<std::size_t>(day)];
    }

    std::string entryStateText(const pesa::Entry &e) {
        switch (e.kind) {
            case pesa::EntryKind::AutoPosted: return "auto";
            case pesa::EntryKind::Edited: return "edited (was " + formatMoney(e.originalAmount.value_or(0)) + ")";
            case pesa::EntryKind::Added: return e.isOneOff() ? "added (one-off)" : "added";
            case pesa::EntryKind::Skipped: return e.skipName.empty()
                                                      ? "skipped (not counted)"
                                                      : "skipped: " + e.skipName;
        }
        return "";
    }

    std::string entryCategoryText(const pesa::Entry &e) {
        return e.isOneOff() ? "One-off" : e.categoryName;
    }

    // ---------------- screens ----------------

    void printDay(Console &io, const pesa::DayView &day) {
        io.out() << weekdayName(day.date.weekday()) << ' ' << day.date.toString() << "\n";

        if (day.entries.empty()) {
            io.out() << "  No entries.\n";
        } else {
            Table table({
                {"ID", Table::Align::Right}, {"Category"}, {"Entry"},
                {"Amount", Table::Align::Right}, {"From"}, {"State"}
            });
            for (const pesa::Entry &e: day.entries) {
                Style style = Style::Normal; // white: untouched auto entry
                if (e.kind == pesa::EntryKind::Edited) style = Style::Yellow;
                if (e.kind == pesa::EntryKind::Added) style = Style::Blue;
                if (e.kind == pesa::EntryKind::Skipped) style = Style::Dim;
                table.addRow({
                                 std::to_string(e.id), entryCategoryText(e), e.name, formatMoney(e.amount),
                                 e.fund == pesa::FundedFrom::Earned ? "earned" : "savings", entryStateText(e)
                             },
                             style);
            }
            table.print(io);
        }
        io.out() << "  Spent " << formatMoney(day.total()) << "  (earned " << formatMoney(day.earnedTotal)
                << ", savings " << formatMoney(day.savingsTotal) << ")\n";
    }

    void printCategories(Console &io, const std::vector<pesa::Category> &categories) {
        if (categories.empty()) {
            io.out() << "  No categories.\n";
            return;
        }
        Table table({{"ID", Table::Align::Right}, {"Category"}, {"Status"}});
        for (const pesa::Category &c: categories) {
            table.addRow({std::to_string(c.id), c.name, statusWord(c.isActive, c.isDeleted, c.isSystem)},
                         statusStyle(c.isActive, c.isDeleted));
        }
        table.print(io);
    }

    void printItems(Console &io, const std::vector<pesa::Item> &items) {
        if (items.empty()) {
            io.out() << "  No items.\n";
            return;
        }
        Table table({
            {"ID", Table::Align::Right}, {"Category"}, {"Item"},
            {"Amount", Table::Align::Right}, {"Status"}
        });
        for (const pesa::Item &i: items) {
            table.addRow({
                             std::to_string(i.id), i.categoryName, i.name, formatMoney(i.defaultAmount),
                             statusWord(i.isActive, i.isDeleted)
                         }, statusStyle(i.isActive, i.isDeleted));
        }
        table.print(io);
    }

    void printRules(Console &io, const std::vector<pesa::Rule> &rules) {
        if (rules.empty()) {
            io.out() << "  No rules.\n";
            return;
        }
        Table table({
            {"ID", Table::Align::Right}, {"Rule"}, {"Repeats"}, {"From"}, {"To"},
            {"Items", Table::Align::Left, 52}, {"Status"}
        });
        for (const pesa::Rule &r: rules) {
            std::string items;
            for (const auto &item: r.items) items += (items.empty() ? "" : ", ") + item.name;
            if (items.empty()) items = "(none)";
            const std::string repeats = r.kind == pesa::RuleKind::Adjust
                                            ? std::string("adjust ") + (r.extra > 0 ? "+" : "") + std::to_string(
                                                  r.extra)
                                            : r.schedule.toString();
            table.addRow({
                             std::to_string(r.id), r.name, repeats, r.effectiveFrom.toString(),
                             r.effectiveTo ? r.effectiveTo->toString() : "never", items,
                             statusWord(r.isActive, r.isDeleted)
                         },
                         statusStyle(r.isActive, r.isDeleted));
        }
        table.print(io);
    }

    void printPreview(std::ostream &out, const pesa::CorrectionPreview &p, const std::string &headline) {
        out << "  " << headline << ": " << p.entries << (p.entries == 1 ? " entry" : " entries") << " across "
                << p.days << (p.days == 1 ? " day" : " days");
        if (p.entries > 0) out << " (KES " << formatMoney(p.before) << " -> " << formatMoney(p.after) << ")";
        out << "\n";
        for (const pesa::ChangedEntry &s: p.sample) {
            out << "    " << weekdayName(s.date.weekday()).substr(0, 3) << ' ' << s.date.toString() << "  " << s.name
                    << "  " << formatMoney(s.before)
                    << (s.after > 0 ? " -> " + formatMoney(s.after) : std::string(" -> no longer counted")) << "\n";
        }
        if (p.entries > static_cast<int>(p.sample.size())) {
            out << "    and " << (p.entries - static_cast<int>(p.sample.size())) << " more\n";
        }
        if (p.keptEdited > 0)
            out << "  " << p.keptEdited << " you edited by hand " << (
                p.keptEdited == 1 ? "is" : "are") << " left alone.\n";
        if (p.keptAdded > 0)
            out << "  " << p.keptAdded << " you added by hand " << (p.keptAdded == 1 ? "is" : "are") <<
                    " left alone.\n";
    }

    void printSkips(Console &io, const std::vector<pesa::Skip> &skips) {
        if (skips.empty()) {
            io.out() << "  No skips.\n";
            return;
        }
        Table table({{"ID", Table::Align::Right}, {"Skip"}, {"From"}, {"To"}, {"Skips", Table::Align::Left, 52}});
        for (const pesa::Skip &s: skips) {
            std::string what;
            for (const auto &t: s.targets) what += (what.empty() ? "" : ", ") + t.name + (t.itemId ? " (item)" : "");
            table.addRow({std::to_string(s.id), s.name, s.from.toString(), s.to.toString(), what});
        }
        table.print(io);
    }

    void printRestorePoints(Console &io, const std::vector<pesa::RestorePoint> &points) {
        if (points.empty()) {
            io.out() <<
                    "  No restore points yet. Pesa saves one before every past-dated rule, skip, wipe and restore.\n";
            return;
        }
        Table table({{"ID", Table::Align::Right}, {"Saved"}, {"What"}, {"Days"}, {"Entries", Table::Align::Right}});
        for (const pesa::RestorePoint &p: points) {
            table.addRow({
                std::to_string(p.id), p.date.toString() + " " + p.time, p.label,
                p.from.toString() + " to " + p.to.toString(), std::to_string(p.entries)
            });
        }
        table.print(io);
    }

    void printCatalog(Console &io, const std::vector<pesa::Category> &categories,
                      const std::vector<pesa::Item> &items) {
        Table table({{"ID", Table::Align::Right}, {"Item"}, {"Amount", Table::Align::Right}, {"Status"}});

        for (const pesa::Category &c: categories) {
            const std::string status = statusWord(c.isActive, c.isDeleted, c.isSystem);
            table.addSection(c.name + "   (category " + std::to_string(c.id) + ")" +
                             (status.empty() ? "" : "  " + status));
            bool any = false;
            for (const pesa::Item &i: items) {
                if (i.categoryId != c.id) continue;
                any = true;
                const std::string itemStatus = statusWord(i.isActive, i.isDeleted);
                table.addRow({std::to_string(i.id), i.name, formatMoney(i.defaultAmount), itemStatus},
                             itemStatus.empty() ? Style::Normal : Style::Dim);
            }
            if (!any) table.addRow({"", "(no items)", "", ""}, Style::Dim);
        }
        table.print(io);
        if (items.empty()) io.out() << "\n  No items yet. Try: catalog add\n";
    }

    void printStatus(std::ostream &out, const pesa::PeriodSummary &s, pesa::Money savingsTotal, pesa::IncomeMode mode) {
        auto line = [&](const char *label, pesa::Money value) {
            out << "  " << std::left << std::setw(20) << label << std::right << std::setw(12) << formatMoney(value) <<
                    "\n";
        };

        out << "Period since " << s.period.start.toString();
        if (s.period.expectedPayday) out << ", payday expected about " << s.period.expectedPayday->toString();
        else if (mode == pesa::IncomeMode::Irregular) out << " (irregular income: no payday to track)";
        out << "\n";
        line("Earned remaining", s.earnedRemaining());
        line("Income", s.income);
        line("Spent from earned", s.spentFromEarned);
        line("Paid from savings", s.spentFromSavings);
        line("Savings", savingsTotal);

        if (s.daysLate) {
            out << "  Payday is " << *s.daysLate << (*s.daysLate == 1 ? " day" : " days")
                    << " late. 'paid' records it; 'delay' moves the date.\n";
        }
        if (s.earnedRemaining() < 0) {
            out <<
                    "  Earned is below zero. That is allowed: add income ('income add') or cover entries ('savings cover').\n";
        }
    }

    void printPaid(std::ostream &out, const pesa::PaidResult &r) {
        out << "Paid on " << r.paidOn.toString() << ": closed the period that started "
                << r.periodStart.toString() << ".\n";
        out << "  Pay added to the new period: " << formatMoney(r.pay) << "\n";
        if (r.movedToSavings > 0) {
            out << "  Leftover " << formatMoney(r.movedToSavings) << " moved to " << r.leftoverPot << ".\n";
        } else if (r.shortfall > 0) {
            out << "  Earned ran " << formatMoney(r.shortfall)
                    << " over. Nothing was taken from savings; use 'savings cover' if you want that.\n";
        } else {
            out << "  Nothing was left over.\n";
        }
        if (r.nextPayday) out << "  Next payday about " << r.nextPayday->toString() << ".\n";
    }

    void printPots(Console &io, const std::vector<pesa::Pot> &pots) {
        Table table({{"ID", Table::Align::Right}, {"Pot"}, {"Kind"}, {"Balance", Table::Align::Right}});
        pesa::Money total = 0;
        for (const pesa::Pot &p: pots) {
            const char *kind = p.kind == pesa::PotKind::Locked
                                   ? "locked"
                                   : p.kind == pesa::PotKind::Tied
                                         ? "contributions only"
                                         : "spendable";
            table.addRow({std::to_string(p.id), p.name, kind, formatMoney(p.balance)});
            total += p.balance;
        }
        table.print(io);
        io.out() << "  Total savings " << formatMoney(total) << "\n";
    }

    void printLedger(Console &io, const std::vector<pesa::LedgerLine> &lines) {
        if (lines.empty()) {
            io.out() << "  No movements yet.\n";
            return;
        }
        Table table({{"Date"}, {"Movement"}, {"Pot"}, {"Amount", Table::Align::Right}});
        for (const pesa::LedgerLine &l: lines) {
            table.addRow({
                l.date.toString(), pesa::ledgerKindName(l.kind) + (l.note.empty() ? "" : " - " + l.note),
                l.potName, formatMoney(l.amount)
            });
        }
        table.print(io);
    }

    void printIncome(Console &io, const std::vector<pesa::IncomeLine> &lines) {
        if (lines.empty()) {
            io.out() << "  No income recorded this period.\n";
            return;
        }
        Table table({{"Date"}, {"From"}, {"Amount", Table::Align::Right}});
        pesa::Money total = 0;
        for (const pesa::IncomeLine &l: lines) {
            table.addRow({l.date.toString(), l.label, formatMoney(l.amount)});
            total += l.amount;
        }
        table.print(io);
        io.out() << "  Total " << formatMoney(total) << "\n";
    }

    void printLog(Console &io, const std::vector<pesa::LogEvent> &events, const pesa::Date &today) {
        if (events.empty()) {
            io.out() << "  No events match.\n";
            return;
        }

        Table table({{"Time"}, {"Type"}, {"What happened", Table::Align::Left, 100}});
        pesa::Date shown; // the day whose heading was printed last
        bool any = false;
        for (const pesa::LogEvent &e: events) {
            if (!any || e.date != shown) {
                shown = e.date;
                any = true;
                const std::string label = e.date == today
                                              ? "Today, "
                                              : e.date == today.previousDay()
                                                    ? "Yesterday, "
                                                    : "";
                table.addSection(label + weekdayName(e.date.weekday()) + " " + e.date.toString());
            }
            table.addRow({e.time, pesa::logKindName(e.kind), e.message},
                         e.kind == pesa::LogKind::Issue ? Style::Yellow : Style::Normal);
        }
        table.print(io);
    }
} // namespace cli
