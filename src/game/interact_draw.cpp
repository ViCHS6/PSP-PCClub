// Отрисовка подсветки отдельно от логики: interact.cpp собирается в тесты без рендера.
#include "game/interact.h"

namespace game {

void InteractSystem::draw_highlight(const core::Mat4& view_projection) const {
    ScreenRect r;
    if (!highlight_rect(view_projection, &r)) return;
    constexpr int kPad = 3;
    constexpr int kThick = 2;
    const render::Color color = render::rgba(0xF0, 0xC0, 0x20, 230);  // акцентный жёлтый, ART.md §2
    const int x0 = r.x - kPad, y0 = r.y - kPad;
    const int x1 = r.x + r.w + kPad, y1 = r.y + r.h + kPad;
    int len = (r.w < r.h ? r.w : r.h) / 3;
    if (len < 4) len = 4;
    if (len > 14) len = 14;
    // Четыре уголка, каждый — две полоски.
    render::draw_rect(x0, y0, len, kThick, color);
    render::draw_rect(x0, y0, kThick, len, color);
    render::draw_rect(x1 - len, y0, len, kThick, color);
    render::draw_rect(x1 - kThick, y0, kThick, len, color);
    render::draw_rect(x0, y1 - kThick, len, kThick, color);
    render::draw_rect(x0, y1 - len, kThick, len, color);
    render::draw_rect(x1 - len, y1 - kThick, len, kThick, color);
    render::draw_rect(x1 - kThick, y1 - len, kThick, len, color);
}

}  // namespace game
