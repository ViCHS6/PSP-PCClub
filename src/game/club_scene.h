#pragma once
// Сцена зала (T014): комната, 6 рабочих мест, стойка админа и полка по data/scene_club.json
// (схема — game/club_scene_layout.h). Каждый ПК связан со своим core::Pc: экран по состоянию
// (render/screen_tex, T042), подсказки и действия по club_scene_rules.h; обслуживание идёт
// мини-игрой в хосте SDK (T050), её quality уходит в команду ядра.
//
// Владение: ClubScene держит пулы Player/Colliders/InteractSystem — объявлять статически.
// World и GameData — у вызывающего, должны жить дольше сцены; world может быть nullptr
// (нет data.pak) — тогда зал рисуется, а ПК не выбираются.
//
// Кадр (main):
//
//     if (minigame::running()) minigame::update(dt, input);
//     ui::update(...);
//     club.update(dt, ui::active() || minigame::running() ? platform::InputState{} : input);
//     if (club.take_os_request()) { /* открыть ОС — до T033 как по START */ }
//     ...
//     club.draw(!ui::active() && !minigame::running());   // подсказки — только в мире
//
// update() зовётся и во время мини-игры: так сцена забирает её результат (take_result)
// и отдаёт команду ядру. Память в кадре не выделяется.
#include "core/core_api.h"
#include "game/club_scene_layout.h"
#include "game/club_scene_rules.h"
#include "game/interact.h"
#include "game/player.h"
#include "minigames/sdk.h"
#include "platform/audio_api.h"
#include "platform/platform_api.h"
#include "render/render_api.h"
#include "render/screen_tex.h"

namespace game {

class ClubScene {
public:
    struct Params {
        core::World* world = nullptr;
        const core::GameData* data = nullptr;
        const char* layout_path = "scene_club.json";
    };

    // При загрузке, после render/audio/ui::init и minigame::register_builtin.
    bool init(const Params& params);
    void update(float dt, const platform::InputState& input);
    void draw(bool hud) const;

    // × у стойки: true один раз — вызывающий открывает ОС (переход камеры — T033).
    bool take_os_request();

    void on_action(InteractId id, InteractAction action);

    int seat_count() const { return seat_count_; }
    const Player& player() const { return player_; }

private:
    struct Seat {
        int16_t number = 0;
        uint8_t pc = 0;
        InteractId monitor = kNoInteract;
        InteractId pc_case = kNoInteract;
        SeatActions actions;
        core::Mat4 desk, monitor_tf, case_tf, chair, screen;
    };
    struct Pending {
        bool active = false;
        int seat = -1;
        PcAction action = PcAction::kNone;
        core::Index game = core::kNone;
        bool pirate = false;
    };

    bool load_layout(const char* path);
    bool build_room();
    bool place_seats();
    bool place_admin();
    void refresh(int seat, bool force);
    void run(int seat, PcAction action);
    void finish_pending();
    void toast(const char* key, int number);
    void draw_hud() const;
    void draw_hint() const;

    ClubLayout layout_;
    core::World* world_ = nullptr;
    const core::GameData* data_ = nullptr;
    Player player_;
    Colliders colliders_;
    InteractSystem interact_;
    Seat seats_[kMaxClubSeats];
    render::ScreenState screens_[kMaxClubSeats];
    int seat_count_ = 0;
    Pending pending_;
    uint32_t attempts_ = 0;
    bool can_repair_ = false;
    bool os_request_ = false;
    InteractId counter_ = kNoInteract;

    const char* toast_text_ = nullptr;
    int toast_number_ = 0;
    float toast_left_ = 0.0f;

    audio::SoundId click_ = audio::kNoSound;
    core::Mat4 projection_{};
    render::MeshId room_ = render::kInvalidMesh;
    render::ModelId desk_model_ = render::kInvalidModel;
    render::ModelId monitor_model_ = render::kInvalidModel;
    render::ModelId case_model_ = render::kInvalidModel;
    render::ModelId chair_model_ = render::kInvalidModel;
    render::ModelId counter_model_ = render::kInvalidModel;
    render::ModelId prop_models_[kMaxClubProps];
    core::Mat4 counter_tf_{}, admin_monitor_tf_{}, admin_case_tf_{}, admin_screen_tf_{};
    core::Mat4 prop_tf_[kMaxClubProps];
    render::ScreenState admin_screen_{};
};

// Демо до игрового цикла (T033/T034): мир в main не тикает, поэтому ПК сами не виснут и не
// пылятся. Расставляет состояния, чтобы на Go было что обслужить: ПК 2 завис, ПК 3 в пыли,
// ПК 4 сломан, ПК 5 выключен, пиратский диск первой вышедшей игры — для Установки.
// Пишет поля World напрямую (кроме выключения) — только для демо-мира, в игре не вызывать.
void club_demo_world(core::World& world);

}  // namespace game
