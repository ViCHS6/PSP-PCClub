#pragma once
// Интерактивные предметы мира (T010): липкий прицел, подсветка, подсказка, колбэк на ×/□.
// GDD §9.1. Пользуются: T014 (6 ПК, стойка, полка), T033 (стойка → вход в ОС), T040.
//
// Владение: InteractSystem — значение с фиксированным пулом на kMaxInteractables слотов,
// живёт у сцены (обычно статически или полем сцены). Память не выделяется никогда.
// Описание (InteractDesc) копируется в слот; строки hint_key/alt_key НЕ копируются — это
// указатели на литералы или на строки data.pak, они должны жить дольше системы.
//
// Кто вызывает и когда:
//   - сцена при загрузке: add() на каждый предмет, сохраняет InteractId;
//   - сцена в кадре: player.update() → interact.update(eye, forward) → interact.handle_input();
//   - колбэк вызывается изнутри handle_input() в главном потоке. В колбэке можно
//     set_enabled/set_hint/set_bounds (в том числе для своего id), нельзя add/clear;
//   - отрисовка: draw_highlight() после 3D, подсказку рисует сцена/UI по hint() (строки
//     игрока — только через ключи strings_ru.json, разрешает их вызывающий через data.pak).
//
// Пример (T014, ПК №4):
//
//     void on_pc(game::InteractId id, game::InteractAction action, void* user) {
//         Club& club = *static_cast<Club*>(user);
//         if (action == game::InteractAction::kPrimary) club.reboot_pc(4);   // ×
//         else club.blow_dust(4);                                           // □
//     }
//     game::InteractDesc d;
//     d.bounds_min = case_min; d.bounds_max = case_max;    // AABB в мире
//     d.aim_point = screen_center;                          // куда тянется прицел
//     d.hint_key = "world.pc.reboot"; d.hint_number = 4;    // «× Перезагрузить ПК 4»
//     d.alt_key = "world.pc.dust";                          // «□ Сдуть пыль»
//     d.on_action = on_pc; d.user = &club;
//     const game::InteractId pc4 = interact.add(d);
//
// T033 (стойка): тот же add() с hint_key = "world.counter.sit", колбэк по × запускает
// переход камеры в монитор; на время ОС сцена просто не зовёт update()/handle_input().
#include <array>
#include <cstdint>

#include "core/math.h"
#include "platform/platform_api.h"
#include "render/render_api.h"

namespace game {

using InteractId = uint8_t;
constexpr InteractId kNoInteract = 0xFF;
// 6 ПК (монитор + системник) + стойка + полка + телефон + коробки; цель — 16 ПК (GDD §1).
constexpr int kMaxInteractables = 48;

enum class InteractAction : uint8_t {
    kPrimary,    // × — взаимодействовать (GDD §9.1)
    kSecondary,  // □ — контекстное действие, только если задан alt_key
};

using InteractCallback = void (*)(InteractId id, InteractAction action, void* user);

struct InteractDesc {
    core::Vec3 bounds_min;           // AABB в мировых координатах: луч взгляда и подсветка
    core::Vec3 bounds_max;
    core::Vec3 aim_point;            // точка, к которой тянется прицел (обычно центр AABB или экрана)
    const char* hint_key = nullptr;  // подсказка для × — ключ strings_ru.json
    const char* alt_key = nullptr;   // подсказка для □; nullptr — у предмета нет □-действия
    int16_t hint_number = -1;        // ≥ 0 — дописывается к подсказке: «Перезагрузить ПК 4»
    float max_distance = 2.0f;       // м от глаз до aim_point, дальше — не выбирается
    InteractCallback on_action = nullptr;
    void* user = nullptr;
    bool enabled = true;
};

// Прямоугольник на экране 480×272, пиксели.
struct ScreenRect {
    int16_t x = 0, y = 0, w = 0, h = 0;
};

// Что показать игроку о предмете под прицелом.
struct InteractHint {
    InteractId id = kNoInteract;
    const char* hint_key = nullptr;
    const char* alt_key = nullptr;
    int16_t hint_number = -1;
};

class InteractSystem {
public:
    // При загрузке сцены. kNoInteract — пул полон или desc без hint_key/колбэка.
    InteractId add(const InteractDesc& desc);
    void clear();  // все слоты свободны, наведения нет
    int count() const { return count_; }

    // Можно в кадре и из колбэка. Неверный id — без последствий.
    void set_enabled(InteractId id, bool enabled);
    void set_hint(InteractId id, const char* hint_key, int16_t hint_number = -1);
    void set_alt(InteractId id, const char* alt_key);
    void set_bounds(InteractId id, core::Vec3 bounds_min, core::Vec3 bounds_max, core::Vec3 aim_point);
    const InteractDesc* desc(InteractId id) const;

    // Раз в кадр после движения игрока: выбирает предмет под прицелом.
    // Сначала — предмет, чей AABB пересекает луч взгляда (ближайший); иначе — предмет с
    // наименьшим углом до aim_point в конусе липкого прицела. У текущей цели конус шире
    // (гистерезис), чтобы подсветка не мигала на границе.
    void update(core::Vec3 eye, core::Vec3 forward);

    // ×/□ из pressed → колбэк предмета под прицелом. true — нажатие обработано
    // (дальше по цепочке его не передавать).
    bool handle_input(const platform::InputState& input);

    InteractId hovered() const { return hovered_; }
    InteractHint hint() const;

    // Точка притяжения прицела для Player; false — цели нет.
    bool aim_target(core::Vec3* out_point) const;

    // Проекция AABB цели на экран, обрезанная по краям. false — цели нет или она вне экрана.
    bool highlight_rect(const core::Mat4& view_projection, ScreenRect* out) const;

    // Подсветка цели: уголки вокруг highlight_rect (2D, после всего 3D; interact_draw.cpp).
    // draw_model не умеет подкрашивать, поэтому подсветка — экранная рамка.
    void draw_highlight(const core::Mat4& view_projection) const;

private:
    struct Slot {
        InteractDesc desc;
        bool used = false;
    };
    std::array<Slot, kMaxInteractables> slots_{};
    int count_ = 0;
    InteractId hovered_ = kNoInteract;
};

// Луч против AABB (slab-метод). true — пересекает на расстоянии [0, max_t], *out_t — вход.
bool ray_aabb(core::Vec3 origin, core::Vec3 dir, core::Vec3 bmin, core::Vec3 bmax, float max_t, float* out_t);

}  // namespace game
