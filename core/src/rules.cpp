//
// Created by lee on 11/24/26.
//
// FTracker/core/src/rules.cpp
#include <pesa/rules.h>

#include "db.h"
#include "util.h"

#include <algorithm>

namespace pesa {

// ---------------- Schedule ----------------

Schedule Schedule::weekly(WeekdaySet days) {
    if (days.empty()) throw Error("A weekly rule needs at least one day.");
    Schedule s;
    s.kind_ = Kind::Weekly;
    s.days_ = days;
    s.monthDay_ = 0;
    return s;
}

Schedule Schedule::monthly(int dayOfMonth) {
    if (dayOfMonth < 1 || dayOfMonth > 31) throw Error("The day of the month must be from 1 to 31.");
    Schedule s;
    s.kind_ = Kind::Monthly;
    s.days_ = WeekdaySet{};
    s.monthDay_ = dayOfMonth;
    return s;
}

Schedule Schedule::fromStored(Kind kind, unsigned weekdayBits, int dayOfMonth) {
    return kind == Kind::Weekly ? weekly(WeekdaySet::fromBits(weekdayBits)) : monthly(dayOfMonth);
}

bool Schedule::occursOn(const Date& date) const {
    if (kind_ == Kind::Weekly) return days_.contains(date.weekday());
    return date.day() == std::min(monthDay_, date.daysInMonth());
}

std::string Schedule::toString() const {
    if (kind_ == Kind::Weekly) return days_.toString();
    return "Monthly, day " + std::to_string(monthDay_);
}

// ---------------- Rule ----------------

bool Rule::appliesOn(const Date& date) const {
    if (isDeleted || !isActive) return false;
    if (date < effectiveFrom) return false;
    if (effectiveTo && date > *effectiveTo) return false;
    return schedule.occursOn(date);
}

// ---------------- Rules (database) ----------------

namespace {

constexpr const char* kEmptyName = "A rule name cannot be empty.";

constexpr const char* kSelectRule =
    "SELECT id, name, repeat_kind, weekdays, month_day, effective_from, effective_to, is_active, is_deleted "
    "FROM rules ";

std::string noRule(RuleId id) { return "No rule with id " + std::to_string(id) + "."; }

// Reads one rule row. Its items are loaded separately (see loadItems).
Rule readRule(const detail::Statement& st) {
    Rule rule;
    rule.id            = st.integer(0);
    rule.name          = st.text(1);
    rule.schedule      = Schedule::fromStored(static_cast<Schedule::Kind>(st.integer(2)),
                                              static_cast<unsigned>(st.integer(3)),
                                              static_cast<int>(st.integer(4)));
    rule.effectiveFrom = Date::parse(st.text(5));
    if (!st.isNull(6)) rule.effectiveTo = Date::parse(st.text(6));
    rule.isActive      = st.integer(7) != 0;
    rule.isDeleted     = st.integer(8) != 0;
    return rule;
}

std::vector<RuleItem> loadItems(detail::Database& db, RuleId id) {
    auto st = db.prepare(
        "SELECT i.id, i.name FROM rule_items ri JOIN items i ON i.id = ri.item_id "
        "WHERE ri.rule_id = ? AND i.is_deleted = 0 ORDER BY i.id");
    st.bind(1, id);
    std::vector<RuleItem> out;
    while (st.step()) out.push_back(RuleItem{st.integer(0), st.text(1)});
    return out;
}

// Names only have to be unique among rules that are NOT deleted.
bool nameTaken(detail::Database& db, const std::string& name, RuleId exceptId) {
    auto st = db.prepare("SELECT 1 FROM rules WHERE name = ? AND id != ? AND is_deleted = 0");
    st.bind(1, name).bind(2, exceptId);
    return st.step();
}

void requireLiveRule(detail::Database& db, RuleId id) {
    auto st = db.prepare("SELECT 1 FROM rules WHERE id = ? AND is_deleted = 0");
    st.bind(1, id);
    if (!st.step()) throw Error(noRule(id));
}

void requireLiveItem(detail::Database& db, ItemId id) {
    auto st = db.prepare("SELECT 1 FROM items WHERE id = ? AND is_deleted = 0");
    st.bind(1, id);
    if (!st.step()) throw Error("No item with id " + std::to_string(id) + ".");
}

// Linking twice is harmless: "ON CONFLICT DO NOTHING" ignores a pair that already exists.
void link(detail::Database& db, RuleId rule, ItemId item) {
    db.prepare("INSERT INTO rule_items(rule_id, item_id) VALUES (?, ?) ON CONFLICT DO NOTHING")
        .bind(1, rule).bind(2, item).run();
}

} // namespace

RuleId Rules::add(const NewRule& rule) {
    const std::string name = detail::cleanName(rule.name, kEmptyName);
    if (rule.effectiveTo && *rule.effectiveTo < rule.effectiveFrom) {
        throw Error("A rule cannot end before it starts.");
    }

    detail::Transaction tx(db_);   // the rule and its item links are saved together or not at all
    if (nameTaken(db_, name, 0)) throw Error("A rule named '" + name + "' already exists.");
    for (const ItemId item : rule.itemIds) requireLiveItem(db_, item);

    auto insert = db_.prepare(
        "INSERT INTO rules(name, repeat_kind, weekdays, month_day, effective_from, effective_to) "
        "VALUES (?, ?, ?, ?, ?, ?)");
    insert.bind(1, name)
          .bind(2, static_cast<std::int64_t>(rule.schedule.kind()))
          .bind(3, static_cast<std::int64_t>(rule.schedule.kind() == Schedule::Kind::Weekly
                                                 ? rule.schedule.weekdays().bits() : 0u))
          .bind(4, static_cast<std::int64_t>(rule.schedule.kind() == Schedule::Kind::Monthly
                                                 ? rule.schedule.dayOfMonth() : 0))
          .bind(5, rule.effectiveFrom.toString());
    if (rule.effectiveTo) insert.bind(6, rule.effectiveTo->toString()); else insert.bindNull(6);
    insert.run();
    const RuleId id = db_.lastInsertId();   // read it straight after the INSERT

    for (const ItemId item : rule.itemIds) link(db_, id, item);
    tx.commit();
    return id;
}

void Rules::rename(RuleId id, std::string_view newName) {
    const std::string name = detail::cleanName(newName, kEmptyName);
    requireLiveRule(db_, id);
    if (nameTaken(db_, name, id)) throw Error("A rule named '" + name + "' already exists.");
    db_.prepare("UPDATE rules SET name = ? WHERE id = ?").bind(1, name).bind(2, id).run();
}

void Rules::setActive(RuleId id, bool active) {
    db_.prepare("UPDATE rules SET is_active = ? WHERE id = ? AND is_deleted = 0")
        .bind(1, active ? 1 : 0).bind(2, id).run();
    if (db_.changes() == 0) throw Error(noRule(id));
}

void Rules::endOn(RuleId id, const Date& lastDay) {
    const auto rule = find(id);
    if (!rule || rule->isDeleted) throw Error(noRule(id));
    if (lastDay < rule->effectiveFrom) {
        throw Error("The rule starts on " + rule->effectiveFrom.toString() + ", so it cannot end before that.");
    }
    db_.prepare("UPDATE rules SET effective_to = ? WHERE id = ?")
        .bind(1, lastDay.toString()).bind(2, id).run();
}

void Rules::addItem(RuleId id, ItemId item) {
    detail::Transaction tx(db_);
    requireLiveRule(db_, id);
    requireLiveItem(db_, item);
    link(db_, id, item);
    tx.commit();
}

void Rules::removeItem(RuleId id, ItemId item) {
    detail::Transaction tx(db_);
    requireLiveRule(db_, id);
    db_.prepare("DELETE FROM rule_items WHERE rule_id = ? AND item_id = ?").bind(1, id).bind(2, item).run();
    if (db_.changes() == 0) throw Error("That item is not part of this rule.");
    tx.commit();
}

void Rules::remove(RuleId id) {
    db_.prepare("UPDATE rules SET is_deleted = 1 WHERE id = ? AND is_deleted = 0").bind(1, id).run();
    if (db_.changes() == 0) throw Error(noRule(id));
}

void Rules::restore(RuleId id) {
    std::string name;
    {
        auto st = db_.prepare("SELECT name FROM rules WHERE id = ? AND is_deleted = 1");
        st.bind(1, id);
        if (!st.step()) throw Error("No deleted rule with id " + std::to_string(id) + ".");
        name = st.text(0);
    }
    if (nameTaken(db_, name, id)) {
        throw Error("Cannot restore: another rule is now called '" + name + "'. Rename it first.");
    }
    db_.prepare("UPDATE rules SET is_deleted = 0 WHERE id = ?").bind(1, id).run();
}

std::optional<Rule> Rules::find(RuleId id) const {
    std::optional<Rule> rule;
    {
        auto st = db_.prepare(std::string(kSelectRule) + "WHERE id = ?");
        st.bind(1, id);
        if (st.step()) rule = readRule(st);
    }
    if (rule) rule->items = loadItems(db_, id);
    return rule;
}

std::vector<Rule> Rules::list(Visibility show) const {
    std::string sql = std::string(kSelectRule) + "WHERE ";
    if (show == Visibility::ActiveOnly)           sql += "is_deleted = 0 AND is_active = 1 ";
    else if (show == Visibility::IncludeInactive) sql += "is_deleted = 0 ";
    else                                          sql += "is_deleted = 1 ";
    sql += "ORDER BY id";

    std::vector<Rule> out;
    {
        auto st = db_.prepare(sql);   // finish this query first...
        while (st.step()) out.push_back(readRule(st));
    }
    for (Rule& rule : out) rule.items = loadItems(db_, rule.id);   // ...then load each rule's items
    return out;
}

} // namespace pesa
