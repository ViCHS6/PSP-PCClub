// Список встроенных мини-игр (sdk.h: register_builtin). Новая игра — одна строка здесь
// (docs/formats/minigame.md §8).
#include "minigames/example/example.h"
#include "minigames/sdk.h"

namespace minigame {

void register_builtin() {
    register_game(example_desc());
}

}  // namespace minigame
