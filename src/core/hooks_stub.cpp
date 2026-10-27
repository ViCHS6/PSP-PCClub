// Заглушки систем, которых ещё нет: рекорды (T053). T053 реализует их в records*.
#include "core/core_api.h"
#include "core/sim_internal.h"

namespace core {

CmdResult cmd_submit_record(World&, Index, int32_t) { return CmdResult::kInvalid; }

}  // namespace core
