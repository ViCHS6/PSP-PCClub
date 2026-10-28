// Список встроенных мини-игр (sdk.h: register_builtin). Новая игра — одна строка здесь
// (docs/formats/minigame.md §8).
#include "minigames/example/example.h"
#include "minigames/service_reboot/service_reboot.h"
#include "minigames/service_clean/service_clean.h"
#include "minigames/service_install/service_install.h"
#include "minigames/sdk.h"

namespace minigame {

void register_builtin() {
    register_game(example_desc());
    register_game(svc_reboot_desc());
    register_game(svc_clean_desc());
    register_game(svc_install_desc());
}

}  // namespace minigame
