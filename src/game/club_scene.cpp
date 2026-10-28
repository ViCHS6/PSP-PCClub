#include "game/club_scene.h"

#include <cstdio>
#include <cstring>
#include <vector>

#include "core/data_api.h"
#include "ui/ui_api.h"

namespace game {
namespace {

using render::rgba;

// scene_club.json сейчас ~3 КиБ; буфер — только на время init().
alignas(16) char g_layout_buf[16 * 1024];

constexpr float kToastTime = 2.0f;
constexpr float kWallThick = 0.3f;

// Модели по id: одна загрузка на id (монитор и системник стойки — те же, что в зале).
struct ModelCache {
    char name[kMaxClubProps + 8][kClubNameMax];
    render::ModelId id[kMaxClubProps + 8];
    int count = 0;
};
ModelCache g_models;

render::ModelId load_model(const char* name) {
    for (int i = 0; i < g_models.count; ++i)
        if (std::strcmp(g_models.name[i], name) == 0) return g_models.id[i];
    char path[64];
    std::snprintf(path, sizeof(path), "assets/%s.msh", name);
    const render::ModelId id = render::model_load_file(path);
    if (id != render::kInvalidModel && g_models.count < static_cast<int>(sizeof(g_models.id) / sizeof(g_models.id[0]))) {
        std::snprintf(g_models.name[g_models.count], kClubNameMax, "%s", name);
        g_models.id[g_models.count++] = id;
    }
    return id;
}

Aabb model_box(render::ModelId model, core::Vec3 pos, float yaw) {
    core::Vec3 lo, hi;
    if (!render::model_bounds(model, &lo, &hi)) return {pos, pos};
    return club_world_box(lo, hi, pos, yaw);
}

void on_club_action(InteractId id, InteractAction action, void* user) {
    static_cast<ClubScene*>(user)->on_action(id, action);
}

// Строки игрока: ключи — литералы, поэтому кэш по указателю, а не по содержимому.
struct TextCache {
    const char* key[16] = {};
    const char* text[16] = {};
};
TextCache g_text;

const char* text(const char* key) {
    if (!key) return "";
    for (int i = 0; i < 16; ++i) {
        if (g_text.key[i] == key) return g_text.text[i];
        if (!g_text.key[i]) {
            g_text.key[i] = key;
            g_text.text[i] = ui::str(key);
            return g_text.text[i];
        }
    }
    return ui::str(key);
}

// Глифы кнопок PSP: × есть в шрифте UI, □ рисуется рамкой (в шрифте нет U+25A1).
enum class Glyph : uint8_t { kNone, kCross, kSquare };

}  // namespace

bool ClubScene::init(const Params& params) {
    world_ = params.world;
    data_ = params.data;
    if (!load_layout(params.layout_path)) return false;

    click_ = audio::sound_find("sfx_click");
    if (click_ == audio::kNoSound) click_ = audio::sound_load_file("assets/sfx_click.wav");
    can_repair_ = minigame::find_game("svc_replace") != minigame::kNoGame;
    render::screens_init();
    if (!build_room() || !place_seats() || !place_admin()) return false;

    const core::Vec3 size = layout_.room.size;
    colliders_.add({{-kWallThick, 0, -kWallThick}, {size.x + kWallThick, size.y, 0.0f}});
    colliders_.add({{-kWallThick, 0, size.z}, {size.x + kWallThick, size.y, size.z + kWallThick}});
    colliders_.add({{-kWallThick, 0, 0}, {0.0f, size.y, size.z}});
    colliders_.add({{size.x, 0, 0}, {size.x + kWallThick, size.y, size.z}});

    for (int i = 0; i < layout_.prop_count; ++i) {
        const ClubPlacement& p = layout_.props[i];
        prop_models_[i] = load_model(p.model);
        if (prop_models_[i] == render::kInvalidModel) return false;
        prop_tf_[i] = club_transform(p.pos, p.yaw);
        if (p.collide) colliders_.add(model_box(prop_models_[i], p.pos, p.yaw));
    }

    // yaw игрока — тот же поворот вокруг +Y, применённый к взгляду в −Z.
    player_.reset(layout_.player_pos, layout_.player_yaw);
    projection_ = core::mat4_perspective(core::deg_to_rad(60.0f),
                                         static_cast<float>(render::kScreenWidth) / render::kScreenHeight,
                                         0.05f, 30.0f);
    for (int i = 0; i < seat_count_; ++i) refresh(i, true);
    return true;
}

bool ClubScene::load_layout(const char* path) {
    size_t size = 0;
    if (!platform::file_read(path, g_layout_buf, sizeof(g_layout_buf), &size)) {
        std::fprintf(stderr, "club_scene: %s не найден\n", path);
        return false;
    }
    char err[160];
    if (!club_layout_parse(g_layout_buf, size, &layout_, err, sizeof(err))) {
        std::fprintf(stderr, "club_scene: %s: %s\n", path, err);
        return false;
    }
    return true;
}

bool ClubScene::build_room() {
    ClubMeshCounts counts;
    club_room_build(layout_, nullptr, 0, nullptr, 0, &counts);
    // Временные буферы — только при загрузке; mesh_create копирует их в арену render.
    std::vector<render::Vertex> v(static_cast<size_t>(counts.vertices));
    std::vector<uint16_t> idx(static_cast<size_t>(counts.indices));
    if (!club_room_build(layout_, v.data(), counts.vertices, idx.data(), counts.indices, &counts)) {
        std::fprintf(stderr, "club_scene: комната больше 65535 вершин/индексов\n");
        return false;
    }
    room_ = render::mesh_create(v.data(), static_cast<uint16_t>(counts.vertices), idx.data(),
                                static_cast<uint16_t>(counts.indices));
    return room_ != render::kInvalidMesh;
}

bool ClubScene::place_seats() {
    const ClubWorkplace& w = layout_.workplace;
    desk_model_ = load_model(w.desk);
    monitor_model_ = load_model(w.monitor);
    case_model_ = load_model(w.pc_case);
    chair_model_ = load_model(w.chair);
    if (desk_model_ == render::kInvalidModel || monitor_model_ == render::kInvalidModel ||
        case_model_ == render::kInvalidModel || chair_model_ == render::kInvalidModel)
        return false;

    seat_count_ = layout_.seat_count;
    for (int i = 0; i < seat_count_; ++i) {
        const ClubSeat& s = layout_.seats[i];
        Seat& seat = seats_[i];
        seat.number = s.id;
        seat.pc = s.pc;
        const ClubSeatPlacement pl = club_seat_placement(layout_, i);
        seat.desk = club_transform(s.pos, s.yaw);
        seat.monitor_tf = club_transform(pl.monitor, s.yaw);
        seat.case_tf = club_transform(pl.pc_case, s.yaw);
        seat.chair = club_transform(pl.chair, pl.chair_yaw);
        seat.screen = seat.monitor_tf * core::mat4_translate(w.screen_pos);
        colliders_.add(model_box(desk_model_, s.pos, s.yaw));
        colliders_.add(model_box(chair_model_, pl.chair, pl.chair_yaw));

        InteractDesc d;
        const Aabb mb = model_box(monitor_model_, pl.monitor, s.yaw);
        d.bounds_min = mb.min;
        d.bounds_max = mb.max;
        d.aim_point = pl.screen;
        d.hint_key = "world.pc.reboot";  // настоящие подсказки ставит refresh()
        d.hint_number = s.id;
        d.max_distance = w.reach;
        d.on_action = on_club_action;
        d.user = this;
        seat.monitor = interact_.add(d);

        const Aabb cb = model_box(case_model_, pl.pc_case, s.yaw);
        d.bounds_min = cb.min;
        d.bounds_max = cb.max;
        d.aim_point = (cb.min + cb.max) * 0.5f;
        d.max_distance = w.case_reach;
        seat.pc_case = interact_.add(d);
        if (seat.monitor == kNoInteract || seat.pc_case == kNoInteract) return false;

        if (world_ && data_) {
            render::screen_state_init(screens_[i], s.pc, world_->pcs[s.pc], *data_);
        } else {
            screens_[i] = render::ScreenState{};
            screens_[i].screen = screens_[i].target = render::ScreenId::kOff;
        }
    }
    return true;
}

bool ClubScene::place_admin() {
    const ClubAdmin& a = layout_.admin;
    counter_model_ = load_model(a.counter);
    if (counter_model_ == render::kInvalidModel) return false;
    const core::Vec3 monitor_pos = a.pos + club_rotate(a.monitor_pos, a.yaw);
    const core::Vec3 case_pos = a.pos + club_rotate(a.case_pos, a.yaw);
    counter_tf_ = club_transform(a.pos, a.yaw);
    admin_monitor_tf_ = club_transform(monitor_pos, a.yaw + a.monitor_yaw);
    admin_case_tf_ = club_transform(case_pos, a.yaw + a.case_yaw);
    admin_screen_tf_ = admin_monitor_tf_ * core::mat4_translate(layout_.workplace.screen_pos);
    const Aabb box = model_box(counter_model_, a.pos, a.yaw);
    colliders_.add(box);

    // Админский ПК — всегда рабочий стол; в ядре его нет (World::pcs — только места зала).
    core::Pc idle{};
    idle.state = core::PcState::kIdle;
    if (data_) {
        render::screen_state_init(admin_screen_, kMaxClubSeats, idle, *data_);
    } else {
        admin_screen_.screen = admin_screen_.target = render::ScreenId::kDesktop;
    }

    // T033 заменит открытие ОС переходом камеры в монитор.
    InteractDesc d;
    const Aabb mb = model_box(monitor_model_, monitor_pos, a.yaw + a.monitor_yaw);
    d.bounds_min = {box.min.x < mb.min.x ? box.min.x : mb.min.x, box.min.y, box.min.z < mb.min.z ? box.min.z : mb.min.z};
    d.bounds_max = {box.max.x > mb.max.x ? box.max.x : mb.max.x, mb.max.y, box.max.z > mb.max.z ? box.max.z : mb.max.z};
    d.aim_point = monitor_pos + club_rotate(layout_.workplace.screen_pos, a.yaw + a.monitor_yaw);
    d.hint_key = "world.counter.sit";
    d.max_distance = a.reach;
    d.on_action = on_club_action;
    d.user = this;
    counter_ = interact_.add(d);
    return counter_ != kNoInteract;
}

void ClubScene::refresh(int i, bool force) {
    Seat& s = seats_[i];
    const SeatActions a = world_ ? seat_actions(*world_, s.pc, can_repair_) : SeatActions{};
    const SeatActions& o = s.actions;
    if (!force && a.monitor == o.monitor && a.case_primary == o.case_primary && a.case_alt == o.case_alt &&
        a.install_game == o.install_game && a.install_pirate == o.install_pirate)
        return;
    s.actions = a;
    interact_.set_enabled(s.monitor, a.monitor != PcAction::kNone);
    if (a.monitor != PcAction::kNone) interact_.set_hint(s.monitor, pc_action_hint(a.monitor), s.number);
    interact_.set_enabled(s.pc_case, a.case_primary != PcAction::kNone);
    if (a.case_primary != PcAction::kNone) interact_.set_hint(s.pc_case, pc_action_hint(a.case_primary), s.number);
    interact_.set_alt(s.pc_case, a.case_alt != PcAction::kNone ? pc_action_hint(a.case_alt) : nullptr);
}

bool ClubScene::take_os_request() {
    const bool r = os_request_;
    os_request_ = false;
    return r;
}

void ClubScene::on_action(InteractId id, InteractAction action) {
    if (id == counter_) {
        os_request_ = true;
        audio::play(click_, 0.8f);
        return;
    }
    for (int i = 0; i < seat_count_; ++i) {
        const Seat& s = seats_[i];
        if (id == s.monitor && action == InteractAction::kPrimary) return run(i, s.actions.monitor);
        if (id == s.pc_case) return run(i, action == InteractAction::kPrimary ? s.actions.case_primary : s.actions.case_alt);
    }
}

void ClubScene::run(int i, PcAction action) {
    if (!world_ || !pc_action_acts(action) || pending_.active || minigame::running()) return;
    Seat& s = seats_[i];
    audio::play(click_, 0.8f);
    const char* game_id = pc_action_minigame(action);
    if (!game_id) {
        pc_action_apply(*world_, s.pc, action, core::kNone, false, 1.0f);
        refresh(i, true);
        return;
    }
    minigame::StartParams p;
    p.kind = minigame::kService;
    p.seed = ++attempts_ * 2654435761u ^ (static_cast<uint32_t>(world_->clock.day_index) << 11) ^
             (static_cast<uint32_t>(world_->clock.minute) << 4) ^ s.pc;
    p.difficulty = pc_action_difficulty(*world_, s.pc, action, s.actions.install_game);
    p.variant = pc_action_variant(*world_, s.pc, action);
    if (!minigame::start(minigame::find_game(game_id), p)) {
        std::fprintf(stderr, "club_scene: мини-игра %s не запустилась\n", game_id);
        return;
    }
    pending_ = {true, i, action, s.actions.install_game, s.actions.install_pirate};
}

void ClubScene::finish_pending() {
    if (!pending_.active || minigame::running()) return;
    const Pending p = pending_;
    pending_ = Pending{};
    minigame::Result r;
    if (!minigame::take_result(&r) || r.outcome == minigame::Outcome::kAborted) return;
    const Seat& s = seats_[p.seat];
    // Провал — quality 0: ядро вернёт kFailed и ничего не изменит (core_api.h).
    const float quality = r.outcome == minigame::Outcome::kCompleted ? r.quality : 0.0f;
    const core::CmdResult res = pc_action_apply(*world_, s.pc, p.action, p.game, p.pirate, quality);
    toast(res == core::CmdResult::kOk ? "world.toast.done" : "world.toast.failed", s.number);
    refresh(p.seat, true);
}

void ClubScene::toast(const char* key, int number) {
    toast_text_ = key;
    toast_number_ = number;
    toast_left_ = kToastTime;
}

void ClubScene::update(float dt, const platform::InputState& input) {
    for (int i = 0; i < seat_count_; ++i) refresh(i, false);

    core::Vec3 aim;
    const bool has_aim = interact_.aim_target(&aim);
    player_.update(dt, input, colliders_, has_aim ? &aim : nullptr);
    interact_.update(player_.eye(), player_.forward());
    interact_.handle_input(input);
    finish_pending();

    if (world_ && data_)
        for (int i = 0; i < seat_count_; ++i) render::screen_select(screens_[i], world_->pcs[seats_[i].pc], *data_);
    render::screens_update(screens_, static_cast<size_t>(seat_count_), dt);
    render::screens_update(&admin_screen_, 1, dt);
    if (toast_left_ > 0.0f) toast_left_ -= dt;
}

void ClubScene::draw(bool hud) const {
    const core::Mat4 view = player_.view();
    render::set_camera(view, projection_);
    render::draw_mesh(room_, core::mat4_identity());
    for (int i = 0; i < seat_count_; ++i) {
        const Seat& s = seats_[i];
        render::draw_model(desk_model_, s.desk);
        render::draw_model(monitor_model_, s.monitor_tf);
        render::draw_model(case_model_, s.case_tf);
        render::draw_model(chair_model_, s.chair);
        render::screen_draw(screens_[i], s.screen);
    }
    render::draw_model(counter_model_, counter_tf_);
    render::draw_model(monitor_model_, admin_monitor_tf_);
    render::draw_model(case_model_, admin_case_tf_);
    render::screen_draw(admin_screen_, admin_screen_tf_);
    for (int i = 0; i < layout_.prop_count; ++i) render::draw_model(prop_models_[i], prop_tf_[i]);

    if (!hud) return;
    interact_.draw_highlight(projection_ * view);
    draw_hud();
}

void ClubScene::draw_hud() const {
    // Точка прицела в центре экрана.
    const render::Color dot = interact_.hovered() != kNoInteract ? rgba(0xF0, 0xC0, 0x20) : rgba(230, 230, 230, 200);
    render::draw_rect(render::kScreenWidth / 2 - 1, render::kScreenHeight / 2 - 1, 2, 2, dot);
    draw_hint();
    if (toast_left_ > 0.0f && toast_text_) {
        char line[96];
        std::snprintf(line, sizeof(line), "%s %d", text(toast_text_), toast_number_);
        const int w = ui::text_width(line);
        const int x = (render::kScreenWidth - w) / 2;
        render::draw_rect(x - 6, 20, w + 12, ui::line_height(ui::Font::kUi12) + 6, rgba(0, 0, 0, 160));
        ui::draw_text(x, 23, line, render::kWhite);
    }
}

void ClubScene::draw_hint() const {
    const InteractHint h = interact_.hint();
    if (h.id == kNoInteract || !h.hint_key) return;

    // Строки подсказки: × действие, □ доп. действие, ниже — что ставим (для Установки).
    const Seat* seat = nullptr;
    PcAction primary = PcAction::kNone;
    for (int i = 0; i < seat_count_; ++i) {
        if (h.id == seats_[i].monitor) primary = seats_[i].actions.monitor;
        else if (h.id == seats_[i].pc_case) primary = seats_[i].actions.case_primary;
        else continue;
        seat = &seats_[i];
        break;
    }
    char lines[3][96];
    Glyph glyph[3] = {};
    int count = 0;
    glyph[count] = !seat || pc_action_acts(primary) ? Glyph::kCross : Glyph::kNone;
    if (h.hint_number >= 0) std::snprintf(lines[count++], sizeof(lines[0]), "%s %d", text(h.hint_key), h.hint_number);
    else std::snprintf(lines[count++], sizeof(lines[0]), "%s", text(h.hint_key));
    if (h.alt_key) {
        glyph[count] = Glyph::kSquare;
        std::snprintf(lines[count++], sizeof(lines[0]), "%s", text(h.alt_key));
    }
    if (seat && primary == PcAction::kInstall && data_ && seat->actions.install_game < data_->game_count) {
        const char* title = core::data_string(*data_, data_->games[seat->actions.install_game].title);
        if (seat->actions.install_pirate)
            std::snprintf(lines[count++], sizeof(lines[0]), "«%s», %s", title, text("world.pc.pirate"));
        else
            std::snprintf(lines[count++], sizeof(lines[0]), "«%s»", title);
    }

    constexpr int kGlyphW = 12;
    const int line_h = ui::line_height(ui::Font::kUi12) + 2;
    int width = 0;
    for (int i = 0; i < count; ++i) {
        const int w = ui::text_width(lines[i]) + (glyph[i] != Glyph::kNone ? kGlyphW : 0);
        if (w > width) width = w;
    }
    const int x = (render::kScreenWidth - width) / 2;
    const int y = render::kScreenHeight - 16 - count * line_h;
    render::draw_rect(x - 6, y - 4, width + 12, count * line_h + 6, rgba(0, 0, 0, 160));
    for (int i = 0; i < count; ++i) {
        const int ly = y + i * line_h;
        int tx = x;
        if (glyph[i] == Glyph::kCross) {
            ui::draw_text(x, ly, "×", rgba(0x9C, 0xB4, 0xE8));
            tx += kGlyphW;
        } else if (glyph[i] == Glyph::kSquare) {
            const render::Color c = rgba(0xE8, 0x9C, 0xC8);  // как цвета кнопок PSP, приглушённо
            render::draw_rect(x + 1, ly + 3, 7, 1, c);
            render::draw_rect(x + 1, ly + 9, 7, 1, c);
            render::draw_rect(x + 1, ly + 3, 1, 7, c);
            render::draw_rect(x + 7, ly + 3, 1, 7, c);
            tx += kGlyphW;
        }
        ui::draw_text(tx, ly, lines[i], render::kWhite);
    }
}

void club_demo_world(core::World& world) {
    if (world.club.seats < 5 || !world.data) return;
    world.pcs[1].state = core::PcState::kHung;
    world.pcs[2].dust = 70.0f;
    world.pcs[3].state = core::PcState::kBroken;
    world.pcs[3].broken_component = core::kCompGpu;
    world.pcs[3].wear[core::kCompGpu] = 85.0f;
    world.pcs[3].dust = 35.0f;
    core::cmd_set_pc_power(world, 4, false);
    for (core::Index g = 0; g < world.data->game_count; ++g) {
        if (core::game_released(world, g)) {
            world.library.pirate[g >> 5] |= 1u << (g & 31);
            break;
        }
    }
}

}  // namespace game
