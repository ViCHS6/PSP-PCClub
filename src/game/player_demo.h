#pragma once
// Демо T010: два рабочих места из заглушек T013 — ходьба, липкий прицел, подсветка,
// подсказки и нажатия. Заменяется сценой зала (T014), которая повторит ту же связку
// Player + Colliders + InteractSystem.
#include "core/data_api.h"
#include "game/interact.h"
#include "game/player.h"
#include "platform/audio_api.h"
#include "platform/platform_api.h"
#include "render/render_api.h"

namespace game {

class PlayerDemo {
public:
    static constexpr int kSeats = 2;

    bool init();
    void update(float dt, const platform::InputState& input);
    void draw() const;

    // Для колбэка взаимодействия.
    void on_action(InteractId id, InteractAction action);

private:
    struct Seat {
        core::Vec3 origin;  // центр стола на полу
        InteractId monitor = kNoInteract;
        InteractId pc = kNoInteract;
        bool on = true;
        float reboot_left = 0.0f;  // с, пока экран чёрный после перезагрузки
        float dust_left = 0.0f;    // с, «облачко» после □
    };

    const char* text(const char* key) const;
    void refresh_hints(int seat);
    void draw_hint() const;

    Player player_;
    Colliders colliders_;
    InteractSystem interact_;
    Seat seats_[kSeats];
    core::GameData data_;
    bool has_data_ = false;
    audio::SoundId click_ = audio::kNoSound;
    core::Mat4 projection_{};

    render::MeshId floor_ = render::kInvalidMesh;
    render::MeshId screen_ = render::kInvalidMesh;
    render::MeshId puff_ = render::kInvalidMesh;
    render::TextureId checker_ = render::kNoTexture;
    render::ModelId desk_ = render::kInvalidModel;
    render::ModelId crt_ = render::kInvalidModel;
    render::ModelId case_ = render::kInvalidModel;
    render::ModelId chair_ = render::kInvalidModel;
};

}  // namespace game
