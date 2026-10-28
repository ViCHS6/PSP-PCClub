#pragma once
// Расстановка зала (T014): data/scene_club.json → ClubLayout, геометрия комнаты с запечённым светом.
// Чистые функции без render/platform — их проверяют тесты. Сцену собирает club_scene.cpp.
//
// Почему не docs/formats: файл читает только club_scene, пишут его руками и экспорт A008 (тоже
// Claude). Это не граница между исполнителями, поэтому схема описана здесь, а не в контракте.
//
// СХЕМА scene_club.json (все поля обязательны, лишние ключи — ошибка; метры, Y вверх):
//   version            1
//   room.size          [ширина X, высота Y, глубина Z]; угол комнаты в (0,0,0)
//   room.tile          сторона плитки, ≤ 0,5 (docs/ART.md §4: GE выбрасывает большие треугольники)
//   room.wall_split    высота границы «панельный зелёный» / побелка
//   room.door          {"x": [x0, x1], "height": h} — проём в стене z = 0
//   room.window        {"z": [z0, z1], "y": [y0, y1]} — окно в стене x = ширина
//   room.colors        RGB 0..255: floor, floor_alt (шахматка линолеума), wall_low, wall_high,
//                      ceiling, window, doorway
//   light.ambient      рассеянный свет 0..1
//   light.lamps[]      ≤ kMaxClubLamps: {"pos", "size": [по X, по Z], "color", "power", "radius"} —
//                      плафон на потолке и точечный источник для запекания в цвета вершин
//   player             {"pos", "yaw"} — старт игрока
//   workplace          модели места и смещения относительно origin стола до поворота места:
//                      desk/monitor/case/chair — id модели (assets/<id>.msh); monitor_pos, case_pos,
//                      chair_pos; screen_pos — центр экрана в координатах монитора; chair_yaw —
//                      доп. поворот стула; reach/case_reach — дальность взаимодействия, м
//   seats[]            ≤ kMaxClubSeats: {"id": номер на табличке ≥ 1, "pc": индекс World::pcs,
//                      "pos", "yaw"}. id и pc уникальны
//   admin              стойка: counter (модель), pos, yaw, monitor_pos/monitor_yaw,
//                      case_pos/case_yaw (относительно стойки), reach
//   props[]            ≤ kMaxClubProps: {"model", "pos", "yaw", "collide"} — прочие пропсы
// Углы yaw — в градусах вокруг +Y по правилу правой руки: +90 переводит +Z в +X
// (как у ассетов, assets/placeholders/README.md). В ClubLayout они уже в радианах.
#include <cstddef>
#include <cstdint>

#include "core/core_api.h"
#include "core/math.h"
#include "game/player.h"
#include "render/render_api.h"

namespace game {

constexpr int kMaxClubSeats = core::kMaxPcs;
constexpr int kMaxClubLamps = 16;
constexpr int kMaxClubProps = 16;
constexpr int kClubNameMax = 32;  // id модели с нулём

struct ClubRgb {
    uint8_t r = 0, g = 0, b = 0;
};

struct ClubRoom {
    core::Vec3 size{};
    float tile = 0.5f;
    float wall_split = 1.4f;
    float door_x0 = 0.0f, door_x1 = 0.0f, door_height = 0.0f;
    float window_z0 = 0.0f, window_z1 = 0.0f, window_y0 = 0.0f, window_y1 = 0.0f;
    ClubRgb floor, floor_alt, wall_low, wall_high, ceiling, window, doorway;
};

struct ClubLamp {
    core::Vec3 pos{};
    float size_x = 0.0f, size_z = 0.0f;
    ClubRgb color;
    float power = 0.0f;
    float radius = 1.0f;
};

struct ClubPlacement {
    char model[kClubNameMax] = {};
    core::Vec3 pos{};
    float yaw = 0.0f;  // радианы
    bool collide = true;
};

struct ClubSeat {
    int16_t id = 0;
    uint8_t pc = 0;
    core::Vec3 pos{};
    float yaw = 0.0f;
};

struct ClubWorkplace {
    char desk[kClubNameMax] = {};
    char monitor[kClubNameMax] = {};
    char pc_case[kClubNameMax] = {};
    char chair[kClubNameMax] = {};
    core::Vec3 monitor_pos{}, screen_pos{}, case_pos{}, chair_pos{};
    float chair_yaw = 0.0f;
    float reach = 2.0f;
    float case_reach = 2.0f;
};

struct ClubAdmin {
    char counter[kClubNameMax] = {};
    core::Vec3 pos{};
    float yaw = 0.0f;
    core::Vec3 monitor_pos{};
    float monitor_yaw = 0.0f;
    core::Vec3 case_pos{};
    float case_yaw = 0.0f;
    float reach = 2.0f;
};

struct ClubLayout {
    ClubRoom room;
    float ambient = 0.5f;
    ClubLamp lamps[kMaxClubLamps];
    int lamp_count = 0;
    core::Vec3 player_pos{};
    float player_yaw = 0.0f;
    ClubWorkplace workplace;
    ClubSeat seats[kMaxClubSeats];
    int seat_count = 0;
    ClubAdmin admin;
    ClubPlacement props[kMaxClubProps];
    int prop_count = 0;
};

// Разбор и проверка. Только при загрузке. false — ошибка: в err «строка N: что не так».
bool club_layout_parse(const char* json, size_t size, ClubLayout* out, char* err, size_t err_cap);

// Поворот вокруг +Y (радианы), затем перенос.
core::Vec3 club_rotate(core::Vec3 p, float yaw);
core::Mat4 club_transform(core::Vec3 pos, float yaw);
// AABB объектного бокса [lo, hi] после поворота и переноса.
Aabb club_world_box(core::Vec3 lo, core::Vec3 hi, core::Vec3 pos, float yaw);

// Мировые позиции частей места seat (origin моделей) и центр экрана.
struct ClubSeatPlacement {
    core::Vec3 monitor, pc_case, chair, screen;
    float yaw = 0.0f;        // стол, монитор, системник
    float chair_yaw = 0.0f;  // стул
};
ClubSeatPlacement club_seat_placement(const ClubLayout& layout, int seat);

// Освещённость точки с нормалью n: ambient + плафоны (Ламберт с «обёрткой» и спадом
// к radius). Результат ≥ 0, обычно 0,5…1,3 — множитель к цвету палитры.
float club_light(const ClubLayout& layout, core::Vec3 p, core::Vec3 n);
// То же по каналам R, G, B с цветом плафонов — им запекаются вершины комнаты.
core::Vec3 club_light_rgb(const ClubLayout& layout, core::Vec3 p, core::Vec3 n);

// Комната: пол, потолок, стены, проём, окно, плафоны — плитками ≤ room.tile, цвет и свет —
// в цветах вершин. vertices/indices == nullptr — только посчитать. false — не хватило cap
// или больше 65535 вершин/индексов.
struct ClubMeshCounts {
    int vertices = 0;
    int indices = 0;
};
bool club_room_build(const ClubLayout& layout, render::Vertex* vertices, int vertex_cap,
                     uint16_t* indices, int index_cap, ClubMeshCounts* out);

}  // namespace game
