#include "ui/apps/os_apps.h"

#include "minigames/games_app.h"
#include "ui/apps/cashier.h"
#if PCCLUB_DEBUG_TOOLS
#include "ui/apps/debug.h"
#endif
#include "ui/apps/ledger.h"
#include "ui/apps/mail.h"
#include "ui/apps/network.h"
#include "ui/apps/records.h"
#include "ui/apps/shop.h"

namespace ui {

// Порядок регистрации = порядок значков на рабочем столе (столбцами сверху вниз).
void register_os_apps() {
    cashier_register();
    network_register(network_minigame_runner());
    mail_register();
    shop_register();
    ledger_register();
    records_register();
    minigame::games_app_register();
#if PCCLUB_DEBUG_TOOLS
    debug_register();  // DBG: последним значком, хост — из main; к релизу выключить (T063)
#endif
}

}  // namespace ui
