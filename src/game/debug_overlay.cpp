#include "game/debug_overlay.h"

#include <cstdio>

#include "render/render_api.h"
#include "platform/audio_api.h"

namespace game {
namespace {

constexpr float kAverageWindow = 0.5f;  // с — усреднение FPS

size_t kib(size_t bytes) { return bytes / 1024; }

}  // namespace

void DebugOverlay::update(float dt, const platform::InputState& input) {
    if (input.pressed & platform::kButtonSelect) visible_ = !visible_;
    input_ = input;
    accum_time_ += dt;
    ++accum_frames_;
    if (accum_time_ >= kAverageWindow) {
        fps_ = accum_frames_ / accum_time_;
        frame_ms_ = accum_time_ * 1000.0f / accum_frames_;
        accum_time_ = 0.0f;
        accum_frames_ = 0;
    }
}

void DebugOverlay::draw() const {
    using render::rgba;
    const render::Color kText = rgba(232, 240, 240);
    const render::Color kAccent = rgba(240, 192, 32);
    constexpr int kLine = render::kDebugGlyphHeight;

    if (!visible_) return;

    const platform::MemoryStats mem = platform::memory_stats();
    const render::FrameStats& rs = render::stats();
    char line[128];
    int y = 4;

    render::draw_rect(2, 2, 468, kLine * 6 + 6, rgba(0, 0, 0, 160));

    // До первого полного окна усреднения — оценка по накопленному.
    float fps = fps_, frame_ms = frame_ms_;
    if (fps == 0.0f && accum_frames_ > 0 && accum_time_ > 0.0f) {
        fps = accum_frames_ / accum_time_;
        frame_ms = accum_time_ * 1000.0f / accum_frames_;
    }
    std::snprintf(line, sizeof(line), "%s  FPS %.1f  кадр %.1f мс", platform::name(),
                  static_cast<double>(fps), static_cast<double>(frame_ms));
    render::debug_text(6, y, line, kAccent);
    y += kLine;

    if (mem.heap_limit > 0) {
        std::snprintf(line, sizeof(line), "Куча %u КиБ (пик %u) из %u", unsigned(kib(mem.heap_used)),
                      unsigned(kib(mem.heap_peak)), unsigned(kib(mem.heap_limit)));
    } else {
        std::snprintf(line, sizeof(line), "Куча %u КиБ (пик %u), без лимита", unsigned(kib(mem.heap_used)),
                      unsigned(kib(mem.heap_peak)));
    }
    render::debug_text(6, y, line, kText);
    y += kLine;

    std::snprintf(line, sizeof(line), "Меши %u/%u КиБ  текстуры %u/%u КиБ  VRAM %u КиБ",
                  unsigned(kib(rs.mesh_arena_used)), unsigned(kib(rs.mesh_arena_size)),
                  unsigned(kib(rs.texture_arena_used)), unsigned(kib(rs.texture_arena_size)),
                  unsigned(kib(rs.vram_used)));
    render::debug_text(6, y, line, kText);
    y += kLine;

    std::snprintf(line, sizeof(line), "Вызовов %u  треугольников %u", unsigned(rs.draw_calls),
                  unsigned(rs.triangles));
    render::debug_text(6, y, line, kText);
    y += kLine;

    struct Name {
        platform::Button button;
        const char* text;
    };
    static const Name kNames[] = {
        {platform::kButtonUp, "↑"},      {platform::kButtonDown, "↓"},     {platform::kButtonLeft, "←"},
        {platform::kButtonRight, "→"},   {platform::kButtonCross, "×"},    {platform::kButtonCircle, "O"},
        {platform::kButtonSquare, "[]"}, {platform::kButtonTriangle, "/\\"}, {platform::kButtonL, "L"},
        {platform::kButtonR, "R"},       {platform::kButtonStart, "START"}, {platform::kButtonSelect, "SEL"},
    };
    int n = std::snprintf(line, sizeof(line), "Стик %+.2f %+.2f  ", static_cast<double>(input_.stick_x),
                          static_cast<double>(input_.stick_y));
    for (const Name& name : kNames) {
        if ((input_.held & name.button) && n < static_cast<int>(sizeof(line)) - 8) {
            n += std::snprintf(line + n, sizeof(line) - n, "%s ", name.text);
        }
    }
    render::debug_text(6, y, line, kText);

    y += kLine;
    const audio::Stats& as = audio::stats();
    std::snprintf(line, sizeof(line), "звук: голоса %u, underrun %u, микс %u/%u мкс",
                  unsigned(as.voices_active), unsigned(as.music_underruns),
                  unsigned(as.mix_us_last), unsigned(as.mix_us_peak));
    render::debug_text(6, y, line, kText);

    render::debug_text(6, render::kScreenHeight - kLine - 4,
                       "Компьютерный клуб · T001 · ×: вращение · SELECT: оверлей", kAccent);
}

}  // namespace game
