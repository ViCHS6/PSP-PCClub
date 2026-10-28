// T014: расстановка зала (scene_club.json), геометрия комнаты, достижимость ПК, правила действий.
#include <doctest/doctest.h>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "core/core_api.h"
#include "core/data_api.h"
#include "game/club_scene_layout.h"
#include "game/club_scene_rules.h"
#include "game/player.h"

using game::ClubLayout;
using game::PcAction;

namespace {

std::string read_file(const char* path) {
    std::string out;
    FILE* f = std::fopen(path, "rb");
    if (!f) return out;
    char buf[4096];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0) out.append(buf, n);
    std::fclose(f);
    return out;
}

bool load_layout(ClubLayout* out) {
    const std::string json = read_file(PCCLUB_DATA_DIR "/scene_club.json");
    char err[160];
    const bool ok = game::club_layout_parse(json.data(), json.size(), out, err, sizeof(err));
    if (!ok) MESSAGE(err);
    return ok;
}

// Ошибка разбора для scene_club.json, в котором заменён кусок.
std::string parse_error(const char* from, const char* to) {
    std::string json = read_file(PCCLUB_DATA_DIR "/scene_club.json");
    const size_t at = json.find(from);
    if (at == std::string::npos) return "<нет образца>";
    json.replace(at, std::strlen(from), to);
    static ClubLayout layout;
    char err[160] = {};
    if (game::club_layout_parse(json.data(), json.size(), &layout, err, sizeof(err))) return "";
    return err;
}

// AABB заглушки T013 по вершинам OBJ.
game::Aabb obj_bounds(const char* id) {
    char path[256];
    std::snprintf(path, sizeof(path), "%s/%s.obj", PCCLUB_PLACEHOLDERS_DIR, id);
    game::Aabb b{{1e9f, 1e9f, 1e9f}, {-1e9f, -1e9f, -1e9f}};
    FILE* f = std::fopen(path, "r");
    REQUIRE(f);
    char line[256];
    while (std::fgets(line, sizeof(line), f)) {
        float x, y, z;
        if (std::sscanf(line, "v %f %f %f", &x, &y, &z) == 3) {
            b.min = {std::fmin(b.min.x, x), std::fmin(b.min.y, y), std::fmin(b.min.z, z)};
            b.max = {std::fmax(b.max.x, x), std::fmax(b.max.y, y), std::fmax(b.max.z, z)};
        }
    }
    std::fclose(f);
    return b;
}

game::Aabb place(const game::Aabb& local, core::Vec3 pos, float yaw) {
    return game::club_world_box(local.min, local.max, pos, yaw);
}

bool overlap_xz(const game::Aabb& a, const game::Aabb& b) {
    return a.min.x < b.max.x - 1e-3f && b.min.x < a.max.x - 1e-3f && a.min.z < b.max.z - 1e-3f &&
           b.min.z < a.max.z - 1e-3f;
}

// Коллайдеры как в ClubScene::init: стены, столы, стулья, стойка, пропсы с collide.
struct Hall {
    game::Colliders colliders;
    std::vector<game::Aabb> furniture;
};

void build_hall(const ClubLayout& l, Hall& h) {
    const core::Vec3 s = l.room.size;
    h.colliders.add({{-0.3f, 0, -0.3f}, {s.x + 0.3f, s.y, 0.0f}});
    h.colliders.add({{-0.3f, 0, s.z}, {s.x + 0.3f, s.y, s.z + 0.3f}});
    h.colliders.add({{-0.3f, 0, 0}, {0.0f, s.y, s.z}});
    h.colliders.add({{s.x, 0, 0}, {s.x + 0.3f, s.y, s.z}});
    const game::Aabb desk = obj_bounds(l.workplace.desk), chair = obj_bounds(l.workplace.chair);
    for (int i = 0; i < l.seat_count; ++i) {
        const game::ClubSeatPlacement p = game::club_seat_placement(l, i);
        h.furniture.push_back(place(desk, l.seats[i].pos, p.yaw));
        h.furniture.push_back(place(chair, p.chair, p.chair_yaw));
    }
    h.furniture.push_back(place(obj_bounds(l.admin.counter), l.admin.pos, l.admin.yaw));
    for (int i = 0; i < l.prop_count; ++i)
        if (l.props[i].collide) h.furniture.push_back(place(obj_bounds(l.props[i].model), l.props[i].pos, l.props[i].yaw));
    for (const game::Aabb& b : h.furniture) REQUIRE(h.colliders.add(b));
}

bool free_at(const Hall& h, float x, float z) {
    const core::Vec3 p{x, 0.0f, z};
    const core::Vec3 r = h.colliders.resolve(p, game::controls::kRadius);
    return std::fabs(r.x - p.x) < 1e-4f && std::fabs(r.z - p.z) < 1e-4f;
}

// Стартовый мир на настоящем data.pak (собирается целью pcclub_data).
struct RealWorld {
    std::vector<uint8_t> pak;
    core::GameData data;
    core::World world;
};

bool real_world(RealWorld& w) {
    const std::string bytes = read_file(PCCLUB_DATA_PAK);
    if (bytes.empty()) return false;
    w.pak.resize(bytes.size() + 16);
    // Буфер должен быть выровнен на 16 (data_api.h).
    uint8_t* base = w.pak.data();
    const size_t shift = (16 - reinterpret_cast<uintptr_t>(base) % 16) % 16;
    std::memcpy(base + shift, bytes.data(), bytes.size());
    if (core::data_load(base + shift, bytes.size(), &w.data) != core::DataError::kOk) return false;
    core::world_init(w.world, w.data, 1);
    return true;
}

}  // namespace

TEST_CASE("T014: scene_club.json — 6 мест, ПК 0..5, плитки ≤ 0,5 м") {
    static ClubLayout l;
    REQUIRE(load_layout(&l));
    CHECK(l.seat_count == 6);
    CHECK(l.room.tile <= 0.5f);
    bool pcs[core::kMaxPcs] = {};
    for (int i = 0; i < l.seat_count; ++i) {
        CHECK(l.seats[i].id == i + 1);
        REQUIRE(l.seats[i].pc < 6);
        CHECK_FALSE(pcs[l.seats[i].pc]);
        pcs[l.seats[i].pc] = true;
    }
    CHECK(l.lamp_count >= 2);
    CHECK(std::string(l.workplace.monitor) == "prop_crt17");
    CHECK(l.player_yaw == doctest::Approx(core::kPi));
}

TEST_CASE("T014: ошибки разбора называют строку и причину") {
    CHECK(parse_error("\"tile\": 0.5", "\"tile\": 0.8").find("room.tile") != std::string::npos);
    CHECK(parse_error("\"tile\": 0.5", "\"tlie\": 0.5").find("неизвестное поле \"tlie\"") != std::string::npos);
    CHECK(parse_error("\"wall_split\": 1.4,", "").find("не хватает полей") != std::string::npos);
    CHECK(parse_error("{\"id\": 2, \"pc\": 1", "{\"id\": 2, \"pc\": 0").find("уникальны") != std::string::npos);
    CHECK(parse_error("\"floor\": [122, 86, 56]", "\"floor\": [122, 86, 300]").find("0..255") != std::string::npos);
    const std::string e = parse_error("\"version\": 1,", "\"version\": 1");
    CHECK(e.rfind("строка 3:", 0) == 0);  // ожидалась ',' перед "room" на строке 3
}

TEST_CASE("T014: комната — плитки ≤ tile, в бюджете ART.md §4, свет запечён") {
    static ClubLayout l;
    REQUIRE(load_layout(&l));
    game::ClubMeshCounts counts;
    REQUIRE(game::club_room_build(l, nullptr, 0, nullptr, 0, &counts));
    std::vector<render::Vertex> v(counts.vertices);
    std::vector<uint16_t> idx(counts.indices);
    REQUIRE(game::club_room_build(l, v.data(), counts.vertices, idx.data(), counts.indices, &counts));
    const int triangles = counts.indices / 3;
    MESSAGE("треугольников в комнате: " << triangles);
    CHECK(triangles <= 3000);
    CHECK(counts.vertices <= 65535);

    float longest = 0.0f;
    for (size_t t = 0; t < idx.size(); t += 3)
        for (int k = 0; k < 3; ++k) {
            const render::Vertex& a = v[idx[t + k]];
            const render::Vertex& b = v[idx[t + (k + 1) % 3]];
            const float dx = a.x - b.x, dy = a.y - b.y, dz = a.z - b.z;
            // Катеты плитки ≤ tile, диагональ ≤ tile·√2.
            longest = std::fmax(longest, std::sqrt(dx * dx + dy * dy + dz * dz));
        }
    CHECK(longest <= l.room.tile * std::sqrt(2.0f) + 1e-3f);

    // Под плафоном пол светлее, чем в углу.
    const float under = game::club_light(l, {l.lamps[0].pos.x, 0, l.lamps[0].pos.z}, {0, 1, 0});
    const float corner = game::club_light(l, {7.9f, 0, 5.9f}, {0, 1, 0});
    CHECK(under > corner + 0.2f);
    CHECK(corner >= l.ambient - 1e-4f);
}

TEST_CASE("T014: мебель не пересекается, все ПК и стойка достижимы от входа") {
    static ClubLayout l;
    REQUIRE(load_layout(&l));
    static Hall h;
    build_hall(l, h);
    for (size_t i = 0; i < h.furniture.size(); ++i)
        for (size_t j = 0; j < i; ++j) {
            INFO("мебель " << i << " и " << j);
            CHECK_FALSE(overlap_xz(h.furniture[i], h.furniture[j]));
        }

    // Заливка проходимых клеток 5 см от старта игрока.
    constexpr float kStep = 0.05f;
    const int nx = static_cast<int>(l.room.size.x / kStep), nz = static_cast<int>(l.room.size.z / kStep);
    std::vector<char> reach(nx * nz, 0);
    const int sx = static_cast<int>(l.player_pos.x / kStep), sz = static_cast<int>(l.player_pos.z / kStep);
    REQUIRE(free_at(h, (sx + 0.5f) * kStep, (sz + 0.5f) * kStep));
    std::vector<int> stack{sz * nx + sx};
    reach[stack.back()] = 1;
    while (!stack.empty()) {
        const int c = stack.back();
        stack.pop_back();
        const int cx = c % nx, cz = c / nx;
        const int nb[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
        for (const auto& d : nb) {
            const int x = cx + d[0], z = cz + d[1];
            if (x < 0 || z < 0 || x >= nx || z >= nz || reach[z * nx + x]) continue;
            if (!free_at(h, (x + 0.5f) * kStep, (z + 0.5f) * kStep)) continue;
            reach[z * nx + x] = 1;
            stack.push_back(z * nx + x);
        }
    }
    // Ближайшая достижимая точка глаз до цели.
    auto nearest = [&](core::Vec3 target) {
        float best = 1e9f;
        for (int z = 0; z < nz; ++z)
            for (int x = 0; x < nx; ++x) {
                if (!reach[z * nx + x]) continue;
                const core::Vec3 eye{(x + 0.5f) * kStep, game::controls::kEyeHeight, (z + 0.5f) * kStep};
                best = std::fmin(best, core::length(target - eye));
            }
        return best;
    };
    const game::Aabb case_box = obj_bounds(l.workplace.pc_case);
    for (int i = 0; i < l.seat_count; ++i) {
        const game::ClubSeatPlacement p = game::club_seat_placement(l, i);
        const game::Aabb cb = place(case_box, p.pc_case, p.yaw);
        INFO("место " << l.seats[i].id);
        CHECK(nearest(p.screen) < l.workplace.reach - 0.2f);
        CHECK(nearest((cb.min + cb.max) * 0.5f) < l.workplace.case_reach - 0.2f);
    }
    const core::Vec3 admin_screen = l.admin.pos + game::club_rotate(l.admin.monitor_pos, l.admin.yaw) +
                                    game::club_rotate(l.workplace.screen_pos, l.admin.yaw + l.admin.monitor_yaw);
    CHECK(nearest(admin_screen) < l.admin.reach - 0.2f);
}

TEST_CASE("T014: действия у ПК по состоянию в ядре") {
    static RealWorld rw;
    REQUIRE(real_world(rw));
    core::World& w = rw.world;
    REQUIRE(w.club.seats >= 6);

    game::SeatActions a = game::seat_actions(w, 0, false);
    CHECK(a.case_primary == PcAction::kPowerOff);
    CHECK(a.monitor == PcAction::kNone);  // на старте нет ни лицензий, ни дисков
    CHECK(a.case_alt == PcAction::kNone);

    // Пиратский диск → Установка с пиратки.
    core::Index g = 0;
    while (g < rw.data.game_count && !core::game_released(w, g)) ++g;
    REQUIRE(g < rw.data.game_count);
    w.library.pirate[g >> 5] |= 1u << (g & 31);
    a = game::seat_actions(w, 0, false);
    CHECK(a.monitor == PcAction::kInstall);
    CHECK(a.install_game == g);
    CHECK(a.install_pirate);

    w.pcs[1].state = core::PcState::kHung;
    CHECK(game::seat_actions(w, 1, false).monitor == PcAction::kReboot);
    CHECK(game::seat_actions(w, 1, false).case_primary == PcAction::kPowerOff);

    w.pcs[2].dust = 50.0f;
    CHECK(game::seat_actions(w, 2, false).case_alt == PcAction::kClean);
    w.pcs[2].state = core::PcState::kBusy;
    CHECK(game::seat_actions(w, 2, false).case_alt == PcAction::kNone);  // ядро ответит kBusy
    CHECK(game::seat_actions(w, 2, false).case_primary == PcAction::kInfoBusy);
    w.pcs[2].state = core::PcState::kIdle;

    w.pcs[3].state = core::PcState::kBroken;
    w.pcs[3].broken_component = core::kCompGpu;
    w.pcs[3].wear[core::kCompGpu] = 80.0f;
    CHECK(game::seat_actions(w, 3, false).case_primary == PcAction::kInfoBroken);
    CHECK(game::seat_actions(w, 3, true).case_primary == PcAction::kRepair);
    w.pcs[3].wear[core::kCompGpu] = 100.0f;  // только замена детали
    CHECK(game::seat_actions(w, 3, true).case_primary == PcAction::kInfoBroken);

    REQUIRE(core::cmd_set_pc_power(w, 4, false) == core::CmdResult::kOk);
    a = game::seat_actions(w, 4, false);
    CHECK(a.case_primary == PcAction::kPowerOn);
    CHECK(a.monitor == PcAction::kNone);

    CHECK(game::seat_actions(w, w.club.seats, false).case_primary == PcAction::kNone);  // места нет
    CHECK(game::pc_action_minigame(PcAction::kReboot) == std::string("svc_reboot"));
    CHECK(game::pc_action_minigame(PcAction::kClean) == std::string("svc_clean"));
    CHECK(game::pc_action_minigame(PcAction::kInstall) == std::string("svc_install"));
    CHECK(game::pc_action_minigame(PcAction::kPowerOn) == nullptr);
    CHECK_FALSE(game::pc_action_acts(PcAction::kInfoBusy));
    for (PcAction act : {PcAction::kInfoBusy, PcAction::kInfoBroken, PcAction::kPowerOn, PcAction::kPowerOff,
                         PcAction::kReboot, PcAction::kClean, PcAction::kInstall, PcAction::kRepair}) {
        const char* key = game::pc_action_hint(act);
        REQUIRE(key);
        INFO(key);
        CHECK(core::data_find_string(rw.data, key) != core::kNone);  // строка есть в data.pak
    }
}

TEST_CASE("T014: quality мини-игры уходит в команду ядра") {
    static RealWorld rw;
    REQUIRE(real_world(rw));
    core::World& w = rw.world;

    w.pcs[1].state = core::PcState::kHung;
    CHECK(game::pc_action_apply(w, 1, PcAction::kReboot, core::kNone, false, 0.0f) == core::CmdResult::kFailed);
    CHECK(w.pcs[1].state == core::PcState::kHung);
    CHECK(game::pc_action_apply(w, 1, PcAction::kReboot, core::kNone, false, 0.8f) == core::CmdResult::kOk);
    CHECK(w.pcs[1].state == core::PcState::kIdle);

    w.pcs[2].dust = 70.0f;
    CHECK(game::pc_action_difficulty(w, 2, PcAction::kClean, core::kNone) == doctest::Approx(0.7f));
    CHECK(game::pc_action_apply(w, 2, PcAction::kClean, core::kNone, false, 0.5f) == core::CmdResult::kOk);
    CHECK(w.pcs[2].dust == doctest::Approx(35.0f));

    core::Index g = 0;
    while (g < rw.data.game_count && !core::game_released(w, g)) ++g;
    w.library.pirate[g >> 5] |= 1u << (g & 31);
    bool pirate = false;
    REQUIRE(game::install_candidate(w, 0, &pirate) == g);
    CHECK(game::pc_action_apply(w, 0, PcAction::kInstall, g, pirate, 0.9f) == core::CmdResult::kOk);
    CHECK(core::pc_has_game(w, 0, g));
    CHECK(game::install_candidate(w, 0, &pirate) != g);  // уже стоит — дальше другая или нет

    CHECK(game::pc_action_apply(w, 3, PcAction::kPowerOff, core::kNone, false, 0.0f) == core::CmdResult::kOk);
    CHECK(w.pcs[3].state == core::PcState::kOff);
    CHECK(game::pc_action_apply(w, 3, PcAction::kPowerOn, core::kNone, false, 0.0f) == core::CmdResult::kOk);
    CHECK(w.pcs[3].state == core::PcState::kIdle);
}
