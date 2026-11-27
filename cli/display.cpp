// FTracker/cli/display.cpp
#include <cli/display.h>
#include <iomanip>
#include <cli/text.h>
#include <algorithm>
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
                case Style::Green: code = "\x1b[32m";
                    break;
                case Style::Cyan: code = "\x1b[36m";
                    break;
                case Style::Magenta: code = "\x1b[35m";
                    break;
                case Style::Red: code = "\x1b[31m";
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
    namespace {
        std::string withExpiry(std::string status, const std::optional<pesa::Date> &lastDay) {
            if (!lastDay) return status;
            return status + (status.empty() ? "" : ", ") + "until " + lastDay->toString();
        }

        Style logStyle(pesa::LogKind kind) {
            switch (kind) {
                case pesa::LogKind::Rule: return Style::Cyan;
                case pesa::LogKind::Manual: return Style::Blue;
                case pesa::LogKind::Edit: return Style::Yellow;
                case pesa::LogKind::Savings: return Style::Magenta;
                case pesa::LogKind::Skip: return Style::Dim;
                case pesa::LogKind::Issue: return Style::Red;
                case pesa::LogKind::Pay: return Style::Green;
            }
            return Style::Normal;
        }
    } // namespace

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
            table.addRow({e.time, pesa::logKindName(e.kind), e.message}, logStyle(e.kind));
        }
        table.print(io);
    }

    void printCategories(Console &io, const std::vector<pesa::Category> &categories) {
        if (categories.empty()) {
            io.out() << "  No categories.\n";
            return;
        }
        Table table({{"ID", Table::Align::Right}, {"Category"}, {"Status"}});
        for (const pesa::Category &c: categories) {
            table.addRow({
                             std::to_string(c.id), c.name,
                             withExpiry(statusWord(c.isActive, c.isDeleted, c.isSystem), c.expiresOn)
                         },
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
                             withExpiry(statusWord(i.isActive, i.isDeleted), i.expiresOn)
                         },
                         statusStyle(i.isActive, i.isDeleted));
        }
        table.print(io);
    }

    void printCatalog(Console &io, const std::vector<pesa::Category> &categories,
                      const std::vector<pesa::Item> &items) {
        Table table({{"ID", Table::Align::Right}, {"Item"}, {"Amount", Table::Align::Right}, {"Status"}});
        for (const pesa::Category &c: categories) {
            const std::string status = withExpiry(statusWord(c.isActive, c.isDeleted, c.isSystem), c.expiresOn);
            table.addSection(c.name + "   (category " + std::to_string(c.id) + ")" +
                             (status.empty() ? "" : "  " + status));
            bool any = false;
            for (const pesa::Item &i: items) {
                if (i.categoryId != c.id) continue;
                any = true;
                const std::string itemStatus = withExpiry(statusWord(i.isActive, i.isDeleted), i.expiresOn);
                table.addRow({std::to_string(i.id), i.name, formatMoney(i.defaultAmount), itemStatus},
                             i.isActive && !i.isDeleted ? Style::Normal : Style::Dim);
            }
            if (!any) table.addRow({"", "(no items)", "", ""}, Style::Dim);
        }
        table.print(io);
        if (items.empty()) io.out() << "\n  No items yet. Try: catalog add\n";
    }

    void printFunds(Console &io, const std::vector<pesa::Fund> &funds, const pesa::Date &today) {
        if (funds.empty()) {
            io.out() << "  No funds. Try: fund add 200,000 Emergency fund --pot <pot> --date <date>\n";
            return;
        }
        Table table({
            {"ID", Table::Align::Right}, {"Fund"}, {"Pot"}, {"Saved", Table::Align::Right},
            {"Target", Table::Align::Right}, {"Done", Table::Align::Right}, {"Date"},
            {"Needed a month", Table::Align::Right}, {"Status"}
        });
        for (const pesa::Fund &f: funds) {
            const pesa::FundProgress p = pesa::fundProgress(f, today);
            const pesa::Money percent = std::clamp<pesa::Money>(f.balance * 100 / f.target, 0, 100);
            const std::string status = p.reached
                                           ? "reached"
                                           : p.overdue
                                                 ? "overdue by " + std::to_string(-p.daysLeft) + " days"
                                                 : f.targetDate
                                                       ? std::to_string(p.daysLeft) + " days left"
                                                       : "";
            table.addRow({
                             std::to_string(f.id), f.name, f.potName, formatMoney(f.balance), formatMoney(f.target),
                             std::to_string(percent) + "%", f.targetDate ? f.targetDate->toString() : "-",
                             p.neededPerMonth > 0 ? formatMoney(p.neededPerMonth) : "-", status
                         },
                         p.reached ? Style::Green : p.overdue ? Style::Red : Style::Normal);
        }
        table.print(io);
    }

    void printPayHistory(Console &io, const std::vector<pesa::PayRate> &rates) {
        if (rates.empty()) {
            io.out() << "  No pay set yet. Try: income pay 70,000\n";
            return;
        }
        Table table({{"From"}, {"To"}, {"Pay", Table::Align::Right}});
        for (const pesa::PayRate &r: rates) {
            table.addRow({r.from.toString(), r.to ? r.to->toString() : "onwards", formatMoney(r.amount)});
        }
        table.print(io);
    }

    void printSources(Console &io, const std::vector<pesa::IncomeSource> &sources, const pesa::Date &today) {
        if (sources.empty()) {
            io.out() <<
                    "  No expected income. Try: income source add 4,500 Dividends --often yearly --next 2026-12-15\n";
            return;
        }
        Table table({
            {"ID", Table::Align::Right}, {"From"}, {"Usual", Table::Align::Right}, {"Often"}, {"Next"}, {"Goes to"}
        });
        for (const pesa::IncomeSource &s: sources) {
            const bool due = s.frequency != pesa::Frequency::WithPay && s.nextExpected && *s.nextExpected <= today;
            table.addRow({
                             std::to_string(s.id), s.name, formatMoney(s.usualAmount), pesa::frequencyName(s.frequency),
                             s.frequency == pesa::Frequency::WithPay
                                 ? "with my pay"
                                 : s.nextExpected
                                       ? s.nextExpected->toString()
                                       : "no plan yet",
                             s.goesTo
                         },
                         due ? Style::Yellow : Style::Normal);
        }
        table.print(io);
    }

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
                Style style = Style::Cyan; // auto-posted by a rule
                switch (e.kind) {
                    case pesa::EntryKind::AutoPosted: style = Style::Cyan;
                        break;
                    case pesa::EntryKind::Edited: style = Style::Yellow;
                        break;
                    case pesa::EntryKind::Added: style = e.isOneOff() ? Style::Magenta : Style::Blue;
                        break;
                    case pesa::EntryKind::Skipped: style = Style::Dim;
                        break;
                }
                table.addRow({
                                 std::to_string(e.id), entryCategoryText(e), e.name, formatMoney(e.amount),
                                 e.fund == pesa::FundedFrom::Earned ? "earned" : "savings", entryStateText(e)
                             },
                             style);
            }
            table.print(io);
            if (io.color()) {
                io.out() << "  " << paint("auto-posted", Style::Cyan, true) << "  " << paint(
                            "edited", Style::Yellow, true)
                        << "  " << paint("added", Style::Blue, true) << "  " << paint("one-off", Style::Magenta, true)
                        << "  " << paint("skipped", Style::Dim, true) << "\n";
            }
        }
        io.out() << "  Spent " << formatMoney(day.total()) << "  (earned " << formatMoney(day.earnedTotal)
                << ", savings " << formatMoney(day.savingsTotal) << ")\n";
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
            out << "  Earned is below zero. That is allowed. Your options: 'income add' (money arrived),\n"
                    "  'savings cover <entry id>' (pay entries from savings), 'settings autocover on' (do that\n"
                    "  automatically), or leave it: 'settings shortfall' decides what happens at payday.\n";
        }
    }

    void printPaid(std::ostream &out, const pesa::PaidResult &r) {
        out << "Paid on " << r.paidOn.toString() << ": closed the period that started "
                << r.periodStart.toString() << ".\n";
        out << "  Pay added to the new period: " << formatMoney(r.pay) << "\n";
        for (const pesa::PaidExtra &extra: r.withPay) {
            out << "  " << extra.name << " (KES " << formatMoney(extra.amount) << ") added with this pay"
                    << (extra.goesTo == "Earned" ? std::string() : ", into " + extra.goesTo) << ".\n";
        }
        if (r.movedToSavings > 0) {
            out << "  Leftover " << formatMoney(r.movedToSavings) << " moved to " << r.leftoverPot << ".\n";
        } else if (r.settledFromSavings > 0) {
            out << "  Earned ran " << formatMoney(r.shortfall) << " over; "
                    << formatMoney(r.settledFromSavings) << " was taken from Main savings.\n";
        } else if (r.carriedForward > 0) {
            out << "  Earned ran " << formatMoney(r.shortfall) << " over; it is the first expense of the new period"
                    << " (\"Overspent last period\").\n";
        } else if (r.shortfall > 0) {
            out << "  Earned ran " << formatMoney(r.shortfall)
                    << " over. Nothing was taken from savings ('settings shortfall' changes that).\n";
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


    void printGoals(Console &io, const std::vector<pesa::GoalStatus> &goals) {
        if (goals.empty()) {
            io.out() << "  No goals to buy. Try: goal add\n";
            return;
        }
        Table table({
            {"ID", Table::Align::Right}, {"Priority", Table::Align::Right}, {"Goal"},
            {"Price", Table::Align::Right}, {"Date"}, {"From"}, {"Status"}
        });
        for (const pesa::GoalStatus &s: goals) {
            const pesa::Goal &g = s.goal;
            std::string status = g.donated
                                     ? std::string("gift, nothing deducted")
                                     : s.covered
                                           ? std::string("covered")
                                           : "short by " + formatMoney(s.shortBy);
            if (g.autoBuy) status += ", buys itself on its date";

            table.addRow({
                             std::to_string(g.id), std::to_string(g.priority), g.name,
                             (g.isEstimate ? "~" : "") + formatMoney(g.amount),
                             g.dueDate ? g.dueDate->toString() : "-",
                             g.fundedFrom == pesa::FundedFrom::Earned ? "earned" : "savings", status
                         },
                         g.donated ? Style::Normal : s.covered ? Style::Green : Style::Yellow);
        }
        table.print(io);
        io.out() << "  Goals are counted in priority order against the money they are paid from.\n";
    }

    void printBoughtGoals(Console &io, const std::vector<pesa::Goal> &goals) {
        if (goals.empty()) {
            io.out() << "  Nothing bought yet.\n";
            return;
        }
        Table table({{"Bought"}, {"Goal"}, {"Paid", Table::Align::Right}, {"Estimated", Table::Align::Right}, {"How"}});
        for (const pesa::Goal &g: goals) {
            const char *how = g.donated
                                  ? "gift"
                                  : !g.boughtHow
                                        ? "-"
                                        : *g.boughtHow == pesa::BuyMethod::FromSavings
                                              ? "savings"
                                              : *g.boughtHow == pesa::BuyMethod::MoveToEarned
                                                    ? "moved to earned"
                                                    : "earned";
            table.addRow({g.boughtOn->toString(), g.name, formatMoney(g.paidAmount.value_or(g.amount)),
                      g.isEstimate ? "~" + formatMoney(g.amount) : "-", how});
        }
        table.print(io);
    }

    void printFunds(Console &io, const std::vector<pesa::Fund> &funds) {
        if (funds.empty()) {
            io.out() << "  No funds. Try: fund add 200,000 Emergency fund --pot <pot>\n";
            return;
        }
        Table table({
            {"ID", Table::Align::Right}, {"Fund"}, {"Pot"}, {"Saved", Table::Align::Right},
            {"Target", Table::Align::Right}, {"Done", Table::Align::Right}
        });
        for (const pesa::Fund &f: funds) {
            const pesa::Money percent = std::clamp<pesa::Money>(f.balance * 100 / f.target, 0, 100);
            table.addRow({
                std::to_string(f.id), f.name, f.potName, formatMoney(f.balance), formatMoney(f.target),
                std::to_string(percent) + "%"
            });
        }
        table.print(io);
    }
} // namespace cli
