#pragma once
// «Пример» (T050) — образец мини-игры по SDK (docs/formats/minigame.md §8, §10).
//
// Правила: ползунок бегает по шкале туда-обратно, × останавливает его. Попал в зелёную зону —
// попадание. Раундов kRounds; на всю попытку kTimeLimit секунд, не успел — оставшиеся раунды
// считаются промахами.
//   difficulty 0…1 — быстрее ползунок и уже зона.
//   score = 100 · попаданий; quality = попаданий / kRounds.
//   kService без единого попадания — Outcome::kFailed.
#include "minigames/sdk.h"

namespace minigame {

constexpr int kExampleRounds = 5;
constexpr float kExampleTimeLimit = 20.0f;

const Desc& example_desc();

}  // namespace minigame
