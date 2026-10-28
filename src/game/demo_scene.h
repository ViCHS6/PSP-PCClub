#pragma once
// ВРЕМЕННАЯ сцена T001/T012: куб и загруженный стол. Заменяется залом клуба (T010, T014).
#include "platform/platform_api.h"
#include "render/render_api.h"
#include "render/anim.h"

namespace game {

class DemoScene {
public:
    bool init();
    void update(float dt, const platform::InputState& input);
    void draw() const;

private:
    render::MeshId cube_ = render::kInvalidMesh;
    render::MeshId floor_ = render::kInvalidMesh;
    render::TextureId checker_ = render::kNoTexture;
    render::ModelId desk_ = render::kInvalidModel;
    render::AnimSet animation_{};
    render::AnimState anim_state_{};
    render::ModelId parts_[render::format::kAnimMaxParts]{};
    uint16_t clip_index_ = 0;
    float yaw_ = 0.6f;
    float pitch_ = 0.35f;
    float spin_ = 0.0f;
    bool auto_spin_ = true;
};

}  // namespace game
