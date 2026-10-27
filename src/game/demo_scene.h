#pragma once
// ВРЕМЕННАЯ сцена T001: вращающийся куб на полу. Заменяется залом клуба (T010, T014).
#include "platform/platform_api.h"
#include "render/render_api.h"

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
    float yaw_ = 0.6f;
    float pitch_ = 0.35f;
    float spin_ = 0.0f;
    bool auto_spin_ = true;
};

}  // namespace game
