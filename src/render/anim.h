#pragma once
// КОНТРАКТ T041: жёсткие части ANM1, см. docs/formats/anim.md.
// Главный поток. Вызывающий владеет ареной, AnimSet, AnimState и массивами матриц/ModelId.
// Арена — фиксированный буфер с базой alignas(16); загрузчик КОПИРУЕТ файл в неё.
// Исходный файл после загрузки можно освободить. AnimSet и арена живут дольше всех
// ссылающихся AnimState; не перемещать AnimSet, пока он используется состояниями.
// Удаления по одному нет: used=0 разрешено только после прекращения всех обращений.
// Загрузка только при старте. update/play/pose/draw не выделяют память.
//
//     alignas(16) static uint8_t bytes[64 * 1024];
//     render::AnimArena arena{bytes, sizeof(bytes), 0};
//     render::AnimSet set{}; render::AnimState state{};
//     if (!render::anim_load_file(arena, "assets/chr_teen_a.anm", &set)) return false;
//     render::anim_start(state, set, render::anim_find_clip(set, "walk"));
//     // Один раз загрузить chr_teen_a_<part.name>.msh в models[part_index].
//     // Кадр: anim_update(state, dt); anim_pose(state, world, matrices, 11);
//     //       anim_draw(set, models, matrices, 11);
//     // Смена: anim_play(state, anim_find_clip(set, "sit_down"));
#include "core/math.h"
#include "render/anim_format.h"
#include "render/render_api.h"

namespace render {

struct AnimArena {
    uint8_t* data;
    size_t size, used;
};
struct AnimSet { format::AnimView view; };

// POD; инициализировать через {} и anim_start. Не менять поля вручную.
struct AnimState {
    const AnimSet* set;
    int16_t clip, previous_clip;
    float time, previous_time, fade_time, fade_duration;
    bool frozen_source;
    // При прерывании перехода сохраняется показанная локальная поза: без скачка.
    format::AnimQuat source_rotation[format::kAnimMaxParts];
    float source_root[3];
};

// При ошибке out обнуляется, arena.used не меняется; error — статическая строка.
// Весь файл проверяется до расходования арены. Буфер источника не должен перекрываться с ареной.
bool anim_load(AnimArena& arena, const void* data, size_t size, AnimSet* out, const char** error = nullptr);
// Временный файловый буфер malloc/free, как model_load_file; путь через platform, диагностика stderr.
bool anim_load_file(AnimArena& arena, const char* path, AnimSet* out);
int16_t anim_find_clip(const AnimSet& set, const char* name); // -1: нет клипа
bool anim_start(AnimState& state, const AnimSet& set, int16_t clip);
// Повтор текущего клипа — no-op; fade_seconds=0 — немедленная смена.
// Во время обычного fade предыдущий клип продолжает идти; при новом play смешанная
// поза становится неподвижным источником. Невалидные аргументы оставляют состояние как было.
bool anim_play(AnimState& state, int16_t clip, float fade_seconds = 0.2f);
// dt в секундах; отрицательные/NaN/Inf игнорируются. Корневой трек не перемещает игровой мир.
void anim_update(AnimState& state, float dt);
// world = положение/ориентация персонажа от T040. out: world-space, column-major,
// порядок частей файла. false при неверном state/ёмкости, out не изменяется.
bool anim_pose(const AnimState& state, const core::Mat4& world, core::Mat4* out, size_t capacity);
// Вызывает draw_model один раз на часть. models/pose — тот же порядок, что в скелете.
void anim_draw(const AnimSet& set, const ModelId* models, const core::Mat4* pose, size_t count);

}  // namespace render
