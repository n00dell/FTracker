// FTracker/core/src/days.cpp
#include <pesa/days.h>

#include "db.h"
#include "entry_sql.h"

namespace pesa {

DayView Days::view(const Date& date) const {
    DayView view;
    view.date = date;

    {
        auto st = db_.prepare(std::string(detail::kSelectEntry) +
                              "WHERE e.date = ? ORDER BY c.sort_order, c.id, e.id");
        st.bind(1, date.toString());
        while (st.step()) view.entries.push_back(detail::readEntry(st));
    }

    for (const Entry& e : view.entries) {
        if (e.kind == EntryKind::Skipped) continue;   // shown in the list, never counted
        (e.fund == FundedFrom::Earned ? view.earnedTotal : view.savingsTotal) += e.amount;
    }

    auto st = db_.prepare("SELECT calculated FROM days WHERE date = ?");
    st.bind(1, date.toString());
    view.calculated = st.step() && st.integer(0) != 0;
    return view;
}

void Days::recalculate(const Date& date) {
    detail::Transaction tx(db_);
    detail::recalculateDay(db_, date);
    tx.commit();
}

std::optional<Money> Days::savedResult(const Date& date) const {
    auto st = db_.prepare("SELECT result FROM days WHERE date = ?");
    st.bind(1, date.toString());
    if (!st.step() || st.isNull(0)) return std::nullopt;
    return st.integer(0);
}

} // namespace pesa
