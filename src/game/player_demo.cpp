#include "game/player_demo.h"

#include <cstdio>

namespace game {
namespace {

using render::rgba;
using render::Vertex;

// data.pak сейчас ~15 КиБ; бюджет GDD §10.3 — до 1 МиБ, для демо хватает меньшего буфера.
alignas(16) uint8_t g_pak[64 * 1024];

// Экран ЭЛТ в координатах prop_crt17 (assets/placeholders: утопленная грань mat_detail).
constexpr float kScreenHalfW = 0.165f;
constexpr float kScreenY0 = 0.155f;
constexpr float kScreenY1 = 0.395f;
constexpr float kScreenZ = 0.197f;  // на 2 мм перед чёрной гранью
constexpr float kDeskTop = 0.75f;
constexpr float kRebootTime = 1.5f;
constexpr float kPuffTime = 0.6f;

render::MeshId create_quad(core::Vec3 a, core::Vec3 b, core::Vec3 c, core::Vec3 d, render::Color color) {
    const Vertex v[4] = {
        {0, 1, color, a.x, a.y, a.z},
        {1, 1, color, b.x, b.y, b.z},
        {1, 0, color, c.x, c.y, c.z},
        {0, 0, color, d.x, d.y, d.z},
    };
    const uint16_t idx[6] = {0, 1, 2, 0, 2, 3};
    return render::mesh_create(v, 4, idx, 6);
}

// Куб со стороной 1 с центром в начале координат, грани — оттенки одного цвета.
render::MeshId create_box(uint8_t r, uint8_t g, uint8_t b) {
    Vertex v[24];
    uint16_t idx[36];
    const float s[6][3] = {{0, 0, 1}, {0, 0, -1}, {1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}};
    const float shade[6] = {1.0f, 0.8f, 0.9f, 0.85f, 1.0f, 0.7f};
    for (int f = 0; f < 6; ++f) {
        const core::Vec3 n = {s[f][0], s[f][1], s[f][2]};
        const core::Vec3 u = (n.y != 0.0f) ? core::Vec3{1, 0, 0} : core::Vec3{n.z, 0, -n.x};
        const core::Vec3 w = core::cross(n, u);
        const render::Color c = rgba(static_cast<uint32_t>(r * shade[f]), static_cast<uint32_t>(g * shade[f]),
                                     static_cast<uint32_t>(b * shade[f]));
        const float k[4][2] = {{-0.5f, -0.5f}, {0.5f, -0.5f}, {0.5f, 0.5f}, {-0.5f, 0.5f}};
        for (int i = 0; i < 4; ++i) {
            const core::Vec3 p = n * 0.5f + u * k[i][0] + w * k[i][1];
            v[f * 4 + i] = {0, 0, c, p.x, p.y, p.z};
        }
        const uint16_t q[6] = {0, 1, 2, 0, 2, 3};
        for (int i = 0; i < 6; ++i) idx[f * 6 + i] = static_cast<uint16_t>(f * 4 + q[i]);
    }
    return render::mesh_create(v, 24, idx, 36);
}

// AABB модели в мире: перенос и, если flip, поворот на 180° вокруг Y.
Aabb world_box(render::ModelId model, core::Vec3 pos, bool flip) {
    core::Vec3 lo, hi;
    if (!render::model_bounds(model, &lo, &hi)) return {pos, pos};
    if (flip) {
        const core::Vec3 t = lo;
        lo = {-hi.x, lo.y, -hi.z};
        hi = {-t.x, hi.y, -t.z};
    }
    return {lo + pos, hi + pos};
}

void on_demo_action(InteractId id, InteractAction action, void* user) {
    static_cast<PlayerDemo*>(user)->on_action(id, action);
}

int utf8_length(const char* s) {
    int n = 0;
    for (; *s; ++s) n += (static_cast<uint8_t>(*s) & 0xC0) != 0x80;
    return n;
}

core::Vec3 monitor_pos(core::Vec3 origin) { return origin + core::Vec3{-0.2f, kDeskTop, -0.1f}; }
core::Vec3 case_pos(core::Vec3 origin) { return origin + core::Vec3{0.35f, kDeskTop, -0.05f}; }
core::Vec3 chair_pos(core::Vec3 origin) { return origin + core::Vec3{0.0f, 0.0f, 0.7f}; }

}  // namespace

bool PlayerDemo::init() {
    size_t size = 0;
    has_data_ = platform::file_read("data.pak", g_pak, sizeof(g_pak), &size) &&
                core::data_load(g_pak, size, &data_) == core::DataError::kOk;
    if (!has_data_) std::fprintf(stderr, "player_demo: нет data.pak — подсказки показываются ключами\n");

    click_ = audio::sound_load_file("assets/sfx_click.wav");
    floor_ = create_quad({-3, 0, 3}, {3, 0, 3}, {3, 0, -3}, {-3, 0, -3}, render::kWhite);
    screen_ = create_quad({-kScreenHalfW, kScreenY0, kScreenZ}, {kScreenHalfW, kScreenY0, kScreenZ},
                          {kScreenHalfW, kScreenY1, kScreenZ}, {-kScreenHalfW, kScreenY1, kScreenZ},
                          rgba(58, 110, 165));  // «рабочий стол» Windows 2000
    puff_ = create_box(200, 196, 184);
    checker_ = render::texture_load_file("assets/demo_checker.tex");
    desk_ = render::model_load_file("assets/prop_desk_chipboard.msh");
    crt_ = render::model_load_file("assets/prop_crt17.msh");
    case_ = render::model_load_file("assets/prop_pc_case.msh");
    chair_ = render::model_load_file("assets/prop_chair_office.msh");
    if (floor_ == render::kInvalidMesh || screen_ == render::kInvalidMesh || puff_ == render::kInvalidMesh ||
        desk_ == render::kInvalidModel || crt_ == render::kInvalidModel || case_ == render::kInvalidModel ||
        chair_ == render::kInvalidModel)
        return false;

    // Стены вокруг пола 6×6 м.
    colliders_.add({{-3.2f, 0, -3.2f}, {3.2f, 2.8f, -3.0f}});
    colliders_.add({{-3.2f, 0, 3.0f}, {3.2f, 2.8f, 3.2f}});
    colliders_.add({{-3.2f, 0, -3.0f}, {-3.0f, 2.8f, 3.0f}});
    colliders_.add({{3.0f, 0, -3.0f}, {3.2f, 2.8f, 3.0f}});

    for (int i = 0; i < kSeats; ++i) {
        Seat& s = seats_[i];
        s.origin = {i == 0 ? -0.7f : 0.7f, 0.0f, -1.5f};
        colliders_.add(world_box(desk_, s.origin, false));
        colliders_.add(world_box(chair_, chair_pos(s.origin), true));

        const core::Vec3 mp = monitor_pos(s.origin);
        const Aabb mb = world_box(crt_, mp, false);
        InteractDesc d;
        d.bounds_min = mb.min;
        d.bounds_max = mb.max;
        d.aim_point = mp + core::Vec3{0.0f, (kScreenY0 + kScreenY1) * 0.5f, kScreenZ};
        d.hint_key = "world.pc.reboot";
        d.hint_number = static_cast<int16_t>(i + 1);
        d.on_action = on_demo_action;
        d.user = this;
        s.monitor = interact_.add(d);

        const Aabb cb = world_box(case_, case_pos(s.origin), false);
        d.bounds_min = cb.min;
        d.bounds_max = cb.max;
        d.aim_point = (cb.min + cb.max) * 0.5f;
        d.alt_key = "world.pc.dust";
        s.pc = interact_.add(d);
        if (s.monitor == kNoInteract || s.pc == kNoInteract) return false;
        refresh_hints(i);
    }

    // Старт: перед вторым местом, прицел чуть мимо монитора — его притянет липкий прицел.
    player_.reset({0.5f, 0.0f, -0.15f}, 0.1f, -0.3f);
    projection_ = core::mat4_perspective(core::deg_to_rad(60.0f),
                                         static_cast<float>(render::kScreenWidth) / render::kScreenHeight,
                                         0.05f, 30.0f);
    return true;
}

const char* PlayerDemo::text(const char* key) const {
    if (!has_data_) return key;
    const core::StrId id = core::data_find_string(data_, key);
    return id == core::kNone ? key : core::data_string(data_, id);
}

void PlayerDemo::refresh_hints(int i) {
    const Seat& s = seats_[i];
    const int16_t number = static_cast<int16_t>(i + 1);
    interact_.set_hint(s.pc, s.on ? "world.pc.power_off" : "world.pc.power_on", number);
    interact_.set_enabled(s.monitor, s.on && s.reboot_left <= 0.0f);
}

void PlayerDemo::on_action(InteractId id, InteractAction action) {
    for (int i = 0; i < kSeats; ++i) {
        Seat& s = seats_[i];
        if (id == s.pc && action == InteractAction::kPrimary) {
            s.on = !s.on;
            s.reboot_left = 0.0f;
        } else if (id == s.pc) {
            s.dust_left = kPuffTime;
        } else if (id == s.monitor) {
            s.reboot_left = kRebootTime;
        } else {
            continue;
        }
        audio::play(click_, 0.8f);
        refresh_hints(i);
    }
}

void PlayerDemo::update(float dt, const platform::InputState& input) {
    core::Vec3 aim;
    const bool has_aim = interact_.aim_target(&aim);
    player_.update(dt, input, colliders_, has_aim ? &aim : nullptr);
    interact_.update(player_.eye(), player_.forward());
    interact_.handle_input(input);

    for (int i = 0; i < kSeats; ++i) {
        Seat& s = seats_[i];
        if (s.dust_left > 0.0f) s.dust_left -= dt;
        if (s.reboot_left > 0.0f) {
            s.reboot_left -= dt;
            if (s.reboot_left <= 0.0f) refresh_hints(i);
        }
    }
}

void PlayerDemo::draw() const {
    const core::Mat4 view = player_.view();
    render::set_camera(view, projection_);
    render::draw_mesh(floor_, core::mat4_identity(), checker_);
    for (const Seat& s : seats_) {
        render::draw_model(desk_, core::mat4_translate(s.origin));
        render::draw_model(crt_, core::mat4_translate(monitor_pos(s.origin)));
        render::draw_model(case_, core::mat4_translate(case_pos(s.origin)));
        render::draw_model(chair_, core::mat4_translate(chair_pos(s.origin)) * core::mat4_rotate_y(core::kPi));
        if (s.on && s.reboot_left <= 0.0f) render::draw_mesh(screen_, core::mat4_translate(monitor_pos(s.origin)));
        if (s.dust_left > 0.0f) {
            const float t = 1.0f - s.dust_left / kPuffTime;  // 0 → 1
            const core::Vec3 p = case_pos(s.origin) + core::Vec3{0.0f, 0.45f + t * 0.25f, 0.0f};
            const float size = 0.06f + t * 0.08f;
            render::draw_mesh(puff_, core::mat4_translate(p) * core::mat4_scale({size, size, size}));
        }
    }

    interact_.draw_highlight(projection_ * view);
    // Точка прицела в центре экрана.
    const render::Color dot = interact_.hovered() != kNoInteract ? rgba(0xF0, 0xC0, 0x20) : rgba(230, 230, 230, 200);
    render::draw_rect(render::kScreenWidth / 2 - 1, render::kScreenHeight / 2 - 1, 2, 2, dot);
    draw_hint();
}

void PlayerDemo::draw_hint() const {
    const InteractHint h = interact_.hint();
    if (h.id == kNoInteract) return;
    // Временно отладочным шрифтом: шрифт UI с глифами кнопок PSP — T030.
    char lines[2][96];
    int count = 0;
    if (h.hint_number >= 0) std::snprintf(lines[count++], sizeof(lines[0]), "× %s %d", text(h.hint_key), h.hint_number);
    else std::snprintf(lines[count++], sizeof(lines[0]), "× %s", text(h.hint_key));
    if (h.alt_key) std::snprintf(lines[count++], sizeof(lines[0]), "[] %s", text(h.alt_key));

    int width = 0;
    for (int i = 0; i < count; ++i) {
        const int w = utf8_length(lines[i]) * render::kDebugGlyphWidth;
        if (w > width) width = w;
    }
    const int line_h = render::kDebugGlyphHeight + 2;
    const int x = (render::kScreenWidth - width) / 2;
    const int y = render::kScreenHeight - 16 - count * line_h;
    render::draw_rect(x - 6, y - 4, width + 12, count * line_h + 6, rgba(0, 0, 0, 160));
    for (int i = 0; i < count; ++i) render::debug_text(x, y + i * line_h, lines[i], render::kWhite);
}

}  // namespace game
