#include "DashController.h"
#include <../gui/src/format.h>

DashController::DashController(AppWindow& ui, pesa::Pesa& core) : ui_(ui), core_(core) {
    // Slint callback  entry-deleted(int)  becomes  on_entry_deleted(...)
    ui_.on_entry_deleted([this](int id) {
        core_.deleteEntry(id);
        refresh();
    });

    // Slint callback  entry-amount-accepted(int, string)
    ui_.on_entry_amount_accepted([this](int id, slint::SharedString text) {
        core_.setEntryAmount(id, std::stoll(std::string(text)));
        refresh();
    });
}

void DashController::refresh() {
    pesa::Money total = 0;
    for (const auto& e : core_.entriesOn(day_)) total += e.amount;

    // Slint property  day-total  becomes  set_day_total(...)
    ui_.set_day_total(slint::SharedString(formatMoney(total)));

    // TODO: build the BucketData model (group entries by category),
    // then ui_.set_buckets(std::make_shared<slint::VectorModel<BucketData>>(...));
}