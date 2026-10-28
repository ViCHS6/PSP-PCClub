#pragma once

#include "ui/ui_api.h"

namespace ui {
struct LedgerPoint {
    core::DayNum date = 0;
    int64_t revenue = 0;
    int64_t expenses = 0;
    int64_t profit = 0;
};

// История от старого дня к новому; незакрытый день в график не входит.
int ledger_history(const core::World& world, LedgerPoint (&out)[core::kStatsDays]);
AppId ledger_register();
}  // namespace ui
