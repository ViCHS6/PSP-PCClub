#pragma once

#include <array>
#include <cstdint>

#include "core/core_api.h"

namespace balance {

enum class Strategy { kPassive, kNormal, kAggressive };

struct Bot {
    Strategy strategy = Strategy::kNormal;
    int32_t morning_day = 0;
    std::array<int32_t, core::kMaxUpgrades> ad_until{};
};

void bot_act(Bot& bot, core::World& world);

}  // namespace balance
