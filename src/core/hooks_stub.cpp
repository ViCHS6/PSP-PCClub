// Заглушки систем, которых ещё нет: события (T023) и рекорды (T053).
// T023 удаляет события отсюда и реализует их в src/core/events*.cpp, T053 — рекорды в records*.
#include "core/core_api.h"
#include "core/sim_internal.h"

namespace core {

namespace detail {

void events_on_action(World&, ActionId, Index) {}
void events_tick(World&) {}

}  // namespace detail

CmdResult cmd_event_choose(World&, uint8_t, uint8_t) { return CmdResult::kInvalid; }

CmdResult cmd_submit_record(World&, Index, int32_t) { return CmdResult::kInvalid; }

}  // namespace core
