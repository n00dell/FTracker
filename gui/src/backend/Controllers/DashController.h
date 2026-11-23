#pragma once

#include "app-window.h"
#include <pesa/pesa.h>

// Wires the Dashboard's callbacks to core, and pushes core data into the UI.
class DashController {
public:
    DashController(AppWindow& ui, pesa::Pesa& core);
    void refresh();

private:
    AppWindow&  ui_;
    pesa::Pesa& core_;
    pesa::Date  day_ = "2026-10-06";   // selected day; later driven by DateBar
};