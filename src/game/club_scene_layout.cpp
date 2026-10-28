#include "game/club_scene_layout.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace game {
namespace {

// ---------------------------------------------------------------------------
// Минимальный разбор JSON под схему из заголовка: объекты, массивы, числа, строки без
// escape-последовательностей, true/false. Только при загрузке.

struct Reader {
    const char* begin;
    const char* p;
    const char* end;
    char* err;
    size_t err_cap;
    bool ok = true;

    int line() const {
        int n = 1;
        for (const char* c = begin; c < p && c < end; ++c) n += *c == '\n';
        return n;
    }
    bool fail(const char* what) {
        if (ok && err && err_cap) std::snprintf(err, err_cap, "строка %d: %s", line(), what);
        ok = false;
        return false;
    }
    void ws() {
        while (p < end && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) ++p;
    }
    bool eat(char c) {
        ws();
        if (p < end && *p == c) {
            ++p;
            return true;
        }
        return false;
    }
    bool expect(char c) {
        if (eat(c)) return true;
        char m[32];
        std::snprintf(m, sizeof(m), "ожидалось '%c'", c);
        return fail(m);
    }
    bool string(char* out, size_t cap) {
        if (!expect('"')) return false;
        size_t n = 0;
        while (p < end && *p != '"') {
            if (*p == '\\' || *p == '\n') return fail("в строках не поддерживаются \\ и перевод строки");
            if (n + 1 >= cap) return fail("слишком длинная строка");
            out[n++] = *p++;
        }
        if (p >= end) return fail("незакрытая строка");
        ++p;
        out[n] = '\0';
        return true;
    }
    bool number(float* out) {
        ws();
        char buf[32];
        size_t n = 0;
        while (p < end && n + 1 < sizeof(buf) &&
               (std::strchr("+-.eE", *p) || (*p >= '0' && *p <= '9')))
            buf[n++] = *p++;
        buf[n] = '\0';
        char* stop = nullptr;
        const float v = std::strtof(buf, &stop);
        if (n == 0 || stop != buf + n || !std::isfinite(v)) return fail("ожидалось число");
        *out = v;
        return true;
    }
    bool boolean(bool* out) {
        ws();
        if (end - p >= 4 && std::strncmp(p, "true", 4) == 0) {
            p += 4;
            *out = true;
            return true;
        }
        if (end - p >= 5 && std::strncmp(p, "false", 5) == 0) {
            p += 5;
            *out = false;
            return true;
        }
        return fail("ожидалось true/false");
    }
    bool floats(float* out, int count) {
        if (!expect('[')) return false;
        for (int i = 0; i < count; ++i) {
            if (i > 0 && !expect(',')) return false;
            if (!number(&out[i])) return false;
        }
        return expect(']');
    }
    bool vec3(core::Vec3* out) {
        float v[3];
        if (!floats(v, 3)) return false;
        *out = {v[0], v[1], v[2]};
        return true;
    }
    bool rgb(ClubRgb* out) {
        float v[3];
        if (!floats(v, 3)) return false;
        for (float c : v)
            if (c < 0.0f || c > 255.0f || c != std::floor(c)) return fail("цвет — целые 0..255");
        *out = {static_cast<uint8_t>(v[0]), static_cast<uint8_t>(v[1]), static_cast<uint8_t>(v[2])};
        return true;
    }
    bool degrees(float* out) {
        if (!number(out)) return false;
        *out = core::deg_to_rad(*out);
        return true;
    }
};

// Объект: field(key, bit) разбирает значение и возвращает true; неизвестный ключ — bit не
// трогает и возвращает false. В конце все биты need должны быть выставлены.
template <class F>
bool object(Reader& r, const char* name, uint32_t need, F&& field) {
    if (!r.expect('{')) return false;
    uint32_t seen = 0;
    if (!r.eat('}')) {
        for (;;) {
            char key[32];
            if (!r.string(key, sizeof(key)) || !r.expect(':')) return false;
            uint32_t bit = 0;
            if (!field(key, &bit)) {
                if (!r.ok) return false;
                char m[96];
                std::snprintf(m, sizeof(m), "%s: неизвестное поле \"%s\"", name, key);
                return r.fail(m);
            }
            if (seen & bit) {
                char m[96];
                std::snprintf(m, sizeof(m), "%s: поле \"%s\" повторяется", name, key);
                return r.fail(m);
            }
            seen |= bit;
            if (r.eat(',')) continue;
            if (!r.expect('}')) return false;
            break;
        }
    }
    if ((seen & need) != need) {
        char m[96];
        std::snprintf(m, sizeof(m), "%s: не хватает полей", name);
        return r.fail(m);
    }
    return true;
}

// Массив объектов: item(index) разбирает элемент; не больше cap элементов.
template <class F>
bool array(Reader& r, const char* name, int cap, int* count, F&& item) {
    if (!r.expect('[')) return false;
    *count = 0;
    if (r.eat(']')) return true;
    for (;;) {
        if (*count >= cap) {
            char m[64];
            std::snprintf(m, sizeof(m), "%s: больше %d элементов", name, cap);
            return r.fail(m);
        }
        if (!item(*count)) return false;
        ++*count;
        if (r.eat(',')) continue;
        return r.expect(']');
    }
}

// Ключ → бит; удобство для лямбд разбора.
bool is(const char* key, const char* name, uint32_t bit, uint32_t* out) {
    if (std::strcmp(key, name) != 0) return false;
    *out = bit;
    return true;
}

bool parse_room(Reader& r, ClubRoom& room) {
    return object(r, "room", 0x3F, [&](const char* k, uint32_t* b) {
        if (is(k, "size", 1, b)) return r.vec3(&room.size);
        if (is(k, "tile", 2, b)) return r.number(&room.tile);
        if (is(k, "wall_split", 4, b)) return r.number(&room.wall_split);
        if (is(k, "door", 8, b))
            return object(r, "room.door", 3, [&](const char* k2, uint32_t* b2) {
                float x[2];
                if (is(k2, "x", 1, b2)) {
                    if (!r.floats(x, 2)) return false;
                    room.door_x0 = x[0];
                    room.door_x1 = x[1];
                    return true;
                }
                if (is(k2, "height", 2, b2)) return r.number(&room.door_height);
                return false;
            });
        if (is(k, "window", 16, b))
            return object(r, "room.window", 3, [&](const char* k2, uint32_t* b2) {
                float v[2];
                if (is(k2, "z", 1, b2)) {
                    if (!r.floats(v, 2)) return false;
                    room.window_z0 = v[0];
                    room.window_z1 = v[1];
                    return true;
                }
                if (is(k2, "y", 2, b2)) {
                    if (!r.floats(v, 2)) return false;
                    room.window_y0 = v[0];
                    room.window_y1 = v[1];
                    return true;
                }
                return false;
            });
        if (is(k, "colors", 32, b))
            return object(r, "room.colors", 0x7F, [&](const char* k2, uint32_t* b2) {
                if (is(k2, "floor", 1, b2)) return r.rgb(&room.floor);
                if (is(k2, "floor_alt", 2, b2)) return r.rgb(&room.floor_alt);
                if (is(k2, "wall_low", 4, b2)) return r.rgb(&room.wall_low);
                if (is(k2, "wall_high", 8, b2)) return r.rgb(&room.wall_high);
                if (is(k2, "ceiling", 16, b2)) return r.rgb(&room.ceiling);
                if (is(k2, "window", 32, b2)) return r.rgb(&room.window);
                if (is(k2, "doorway", 64, b2)) return r.rgb(&room.doorway);
                return false;
            });
        return false;
    });
}

bool parse_light(Reader& r, ClubLayout& out) {
    return object(r, "light", 3, [&](const char* k, uint32_t* b) {
        if (is(k, "ambient", 1, b)) return r.number(&out.ambient);
        if (is(k, "lamps", 2, b))
            return array(r, "light.lamps", kMaxClubLamps, &out.lamp_count, [&](int i) {
                ClubLamp& l = out.lamps[i];
                return object(r, "lamp", 0x1F, [&](const char* k2, uint32_t* b2) {
                    float s[2];
                    if (is(k2, "pos", 1, b2)) return r.vec3(&l.pos);
                    if (is(k2, "size", 2, b2)) {
                        if (!r.floats(s, 2)) return false;
                        l.size_x = s[0];
                        l.size_z = s[1];
                        return true;
                    }
                    if (is(k2, "color", 4, b2)) return r.rgb(&l.color);
                    if (is(k2, "power", 8, b2)) return r.number(&l.power);
                    if (is(k2, "radius", 16, b2)) return r.number(&l.radius);
                    return false;
                });
            });
        return false;
    });
}

bool parse_workplace(Reader& r, ClubWorkplace& w) {
    return object(r, "workplace", 0x7FF, [&](const char* k, uint32_t* b) {
        if (is(k, "desk", 1, b)) return r.string(w.desk, sizeof(w.desk));
        if (is(k, "monitor", 2, b)) return r.string(w.monitor, sizeof(w.monitor));
        if (is(k, "case", 4, b)) return r.string(w.pc_case, sizeof(w.pc_case));
        if (is(k, "chair", 8, b)) return r.string(w.chair, sizeof(w.chair));
        if (is(k, "monitor_pos", 16, b)) return r.vec3(&w.monitor_pos);
        if (is(k, "screen_pos", 32, b)) return r.vec3(&w.screen_pos);
        if (is(k, "case_pos", 64, b)) return r.vec3(&w.case_pos);
        if (is(k, "chair_pos", 128, b)) return r.vec3(&w.chair_pos);
        if (is(k, "chair_yaw", 256, b)) return r.degrees(&w.chair_yaw);
        if (is(k, "reach", 512, b)) return r.number(&w.reach);
        if (is(k, "case_reach", 1024, b)) return r.number(&w.case_reach);
        return false;
    });
}

bool parse_admin(Reader& r, ClubAdmin& a) {
    return object(r, "admin", 0xFF, [&](const char* k, uint32_t* b) {
        if (is(k, "counter", 1, b)) return r.string(a.counter, sizeof(a.counter));
        if (is(k, "pos", 2, b)) return r.vec3(&a.pos);
        if (is(k, "yaw", 4, b)) return r.degrees(&a.yaw);
        if (is(k, "monitor_pos", 8, b)) return r.vec3(&a.monitor_pos);
        if (is(k, "monitor_yaw", 16, b)) return r.degrees(&a.monitor_yaw);
        if (is(k, "case_pos", 32, b)) return r.vec3(&a.case_pos);
        if (is(k, "case_yaw", 64, b)) return r.degrees(&a.case_yaw);
        if (is(k, "reach", 128, b)) return r.number(&a.reach);
        return false;
    });
}

bool parse_root(Reader& r, ClubLayout& out) {
    return object(r, "scene_club", 0xFF, [&](const char* k, uint32_t* b) {
        if (is(k, "version", 1, b)) {
            float v = 0.0f;
            if (!r.number(&v)) return false;
            return v == 1.0f || r.fail("version: поддерживается только 1");
        }
        if (is(k, "room", 2, b)) return parse_room(r, out.room);
        if (is(k, "light", 4, b)) return parse_light(r, out);
        if (is(k, "player", 8, b))
            return object(r, "player", 3, [&](const char* k2, uint32_t* b2) {
                if (is(k2, "pos", 1, b2)) return r.vec3(&out.player_pos);
                if (is(k2, "yaw", 2, b2)) return r.degrees(&out.player_yaw);
                return false;
            });
        if (is(k, "workplace", 16, b)) return parse_workplace(r, out.workplace);
        if (is(k, "seats", 32, b))
            return array(r, "seats", kMaxClubSeats, &out.seat_count, [&](int i) {
                ClubSeat& s = out.seats[i];
                return object(r, "seat", 0xF, [&](const char* k2, uint32_t* b2) {
                    float v = 0.0f;
                    if (is(k2, "id", 1, b2)) {
                        if (!r.number(&v)) return false;
                        if (v < 1.0f || v > 99.0f || v != std::floor(v)) return r.fail("seat.id — целое 1..99");
                        s.id = static_cast<int16_t>(v);
                        return true;
                    }
                    if (is(k2, "pc", 2, b2)) {
                        if (!r.number(&v)) return false;
                        if (v < 0.0f || v >= core::kMaxPcs || v != std::floor(v))
                            return r.fail("seat.pc — индекс 0..15");
                        s.pc = static_cast<uint8_t>(v);
                        return true;
                    }
                    if (is(k2, "pos", 4, b2)) return r.vec3(&s.pos);
                    if (is(k2, "yaw", 8, b2)) return r.degrees(&s.yaw);
                    return false;
                });
            });
        if (is(k, "admin", 64, b)) return parse_admin(r, out.admin);
        if (is(k, "props", 128, b))
            return array(r, "props", kMaxClubProps, &out.prop_count, [&](int i) {
                ClubPlacement& p = out.props[i];
                return object(r, "prop", 0xF, [&](const char* k2, uint32_t* b2) {
                    if (is(k2, "model", 1, b2)) return r.string(p.model, sizeof(p.model));
                    if (is(k2, "pos", 2, b2)) return r.vec3(&p.pos);
                    if (is(k2, "yaw", 4, b2)) return r.degrees(&p.yaw);
                    if (is(k2, "collide", 8, b2)) return r.boolean(&p.collide);
                    return false;
                });
            });
        return false;
    });
}

// Смысловые проверки после разбора.
bool validate(Reader& r, const ClubLayout& l) {
    const ClubRoom& m = l.room;
    if (m.size.x <= 0.0f || m.size.y <= 0.0f || m.size.z <= 0.0f) return r.fail("room.size должен быть > 0");
    if (m.tile < 0.05f || m.tile > 0.5f) return r.fail("room.tile — 0,05…0,5 м (docs/ART.md §4)");
    if (!(m.door_x0 > 0.0f && m.door_x0 < m.door_x1 && m.door_x1 < m.size.x && m.door_height > 0.0f &&
          m.door_height < m.size.y))
        return r.fail("room.door вне стены z = 0");
    if (!(m.window_z0 > 0.0f && m.window_z0 < m.window_z1 && m.window_z1 < m.size.z && m.window_y0 > 0.0f &&
          m.window_y0 < m.window_y1 && m.window_y1 < m.size.y))
        return r.fail("room.window вне стены x = ширина");
    if (l.seat_count == 0) return r.fail("seats пуст");
    for (int i = 0; i < l.seat_count; ++i)
        for (int j = 0; j < i; ++j)
            if (l.seats[i].id == l.seats[j].id || l.seats[i].pc == l.seats[j].pc)
                return r.fail("seats: id и pc должны быть уникальны");
    for (int i = 0; i < l.lamp_count; ++i)
        if (l.lamps[i].radius <= 0.0f || l.lamps[i].size_x <= 0.0f || l.lamps[i].size_z <= 0.0f)
            return r.fail("lamp: size и radius должны быть > 0");
    return true;
}

// ---------------------------------------------------------------------------
// Геометрия комнаты

constexpr int kMaxBreaks = 64;

// Точки разбиения отрезка [0, len]: шаг tile плюс края проёмов/окна/полосы краски.
int breakpoints(float len, float tile, const float* extra, int extra_count, float* out) {
    int n = 0;
    const int steps = static_cast<int>(std::ceil(len / tile - 1e-4f));
    for (int i = 0; i <= steps && n < kMaxBreaks; ++i) out[n++] = i == steps ? len : tile * i;
    for (int i = 0; i < extra_count && n < kMaxBreaks; ++i)
        if (extra[i] > 1e-3f && extra[i] < len - 1e-3f) out[n++] = extra[i];
    for (int i = 1; i < n; ++i)  // вставками: точек мало
        for (int j = i; j > 0 && out[j] < out[j - 1]; --j) {
            const float t = out[j];
            out[j] = out[j - 1];
            out[j - 1] = t;
        }
    int m = 0;
    for (int i = 0; i < n; ++i)
        if (m == 0 || out[i] - out[m - 1] > 1e-3f) out[m++] = out[i];
    return m;
}

render::Color shade(ClubRgb c, core::Vec3 k) {
    auto ch = [](uint8_t v, float m) {
        const float f = v * m;
        return static_cast<uint32_t>(f > 255.0f ? 255.0f : f);
    };
    return render::rgba(ch(c.r, k.x), ch(c.g, k.y), ch(c.b, k.z));
}

// Ячейка поверхности: цвет и освещать ли (окно, проём, плафоны светятся сами).
struct Cell {
    bool draw = true;
    ClubRgb color;
    bool lit = true;
};

struct Builder {
    const ClubLayout& layout;
    render::Vertex* v;
    int v_cap;
    uint16_t* idx;
    int i_cap;
    int vn = 0;
    int in = 0;
    bool overflow = false;

    void quad(const core::Vec3 p[4], core::Vec3 n, const Cell& cell) {
        if (vn + 4 > 65535 || in + 6 > 65535) overflow = true;
        if (v && idx && !overflow) {
            if (vn + 4 > v_cap || in + 6 > i_cap) {
                overflow = true;
            } else {
                for (int k = 0; k < 4; ++k) {
                    core::Vec3 light = cell.lit ? club_light_rgb(layout, p[k], n) : core::Vec3{1, 1, 1};
                    // Тень у пола: стены и проём темнеют к низу (дешёвый ambient occlusion).
                    if (cell.lit && n.y == 0.0f && p[k].y < 0.4f) light = light * (0.8f + 0.5f * p[k].y);
                    v[vn + k] = {0.0f, 0.0f, shade(cell.color, light), p[k].x, p[k].y, p[k].z};
                }
                const uint16_t b = static_cast<uint16_t>(vn);
                const uint16_t q[6] = {b, static_cast<uint16_t>(b + 1), static_cast<uint16_t>(b + 2),
                                       b, static_cast<uint16_t>(b + 2), static_cast<uint16_t>(b + 3)};
                for (int k = 0; k < 6; ++k) idx[in + k] = q[k];
            }
        }
        vn += 4;
        in += 6;
    }

    // Прямоугольник origin + a·u + b·w, a ∈ [0, len_u], b ∈ [0, len_w], разбитый по точкам.
    // cell(a_center, b_center) решает цвет ячейки.
    template <class F>
    void surface(core::Vec3 origin, core::Vec3 u, core::Vec3 w, core::Vec3 n, float len_u, float len_w,
                 const float* extra_u, int nu, const float* extra_w, int nw, F&& cell) {
        float bu[kMaxBreaks], bw[kMaxBreaks];
        const float tile = layout.room.tile;
        const int cu = breakpoints(len_u, tile, extra_u, nu, bu);
        const int cw = breakpoints(len_w, tile, extra_w, nw, bw);
        for (int j = 0; j + 1 < cw; ++j) {
            for (int i = 0; i + 1 < cu; ++i) {
                const Cell c = cell(0.5f * (bu[i] + bu[i + 1]), 0.5f * (bw[j] + bw[j + 1]));
                if (!c.draw) continue;
                const core::Vec3 p[4] = {origin + u * bu[i] + w * bw[j], origin + u * bu[i + 1] + w * bw[j],
                                         origin + u * bu[i + 1] + w * bw[j + 1], origin + u * bu[i] + w * bw[j + 1]};
                quad(p, n, c);
            }
        }
    }
};

}  // namespace

bool club_layout_parse(const char* json, size_t size, ClubLayout* out, char* err, size_t err_cap) {
    if (err && err_cap) err[0] = '\0';
    *out = ClubLayout{};
    Reader r{json, json, json + size, err, err_cap};
    if (!parse_root(r, *out)) return false;
    r.ws();
    if (r.p != r.end) return r.fail("лишние символы после объекта");
    return validate(r, *out);
}

core::Vec3 club_rotate(core::Vec3 p, float yaw) {
    const float c = std::cos(yaw), s = std::sin(yaw);
    return {c * p.x + s * p.z, p.y, -s * p.x + c * p.z};  // как core::mat4_rotate_y
}

core::Mat4 club_transform(core::Vec3 pos, float yaw) {
    return core::mat4_translate(pos) * core::mat4_rotate_y(yaw);
}

Aabb club_world_box(core::Vec3 lo, core::Vec3 hi, core::Vec3 pos, float yaw) {
    Aabb box{{1e9f, 1e9f, 1e9f}, {-1e9f, -1e9f, -1e9f}};
    for (int i = 0; i < 8; ++i) {
        const core::Vec3 c = {(i & 1) ? hi.x : lo.x, (i & 2) ? hi.y : lo.y, (i & 4) ? hi.z : lo.z};
        const core::Vec3 p = club_rotate(c, yaw) + pos;
        box.min = {std::fmin(box.min.x, p.x), std::fmin(box.min.y, p.y), std::fmin(box.min.z, p.z)};
        box.max = {std::fmax(box.max.x, p.x), std::fmax(box.max.y, p.y), std::fmax(box.max.z, p.z)};
    }
    return box;
}

ClubSeatPlacement club_seat_placement(const ClubLayout& layout, int seat) {
    const ClubWorkplace& w = layout.workplace;
    const ClubSeat& s = layout.seats[seat];
    ClubSeatPlacement p;
    p.yaw = s.yaw;
    p.chair_yaw = s.yaw + w.chair_yaw;
    p.monitor = s.pos + club_rotate(w.monitor_pos, s.yaw);
    p.pc_case = s.pos + club_rotate(w.case_pos, s.yaw);
    p.chair = s.pos + club_rotate(w.chair_pos, s.yaw);
    p.screen = p.monitor + club_rotate(w.screen_pos, s.yaw);
    return p;
}

// Вклад плафонов по каналам: тёплая лампа у стойки подкрашивает её угол (ART.md §2).
core::Vec3 club_light_rgb(const ClubLayout& layout, core::Vec3 p, core::Vec3 n) {
    constexpr float kWrap = 0.35f;  // «обёртка» Ламберта: потолок и стены у плафона не чёрные
    core::Vec3 light{layout.ambient, layout.ambient, layout.ambient};
    for (int i = 0; i < layout.lamp_count; ++i) {
        const ClubLamp& lamp = layout.lamps[i];
        const core::Vec3 d = lamp.pos - p;
        const float dist = core::length(d);
        if (dist >= lamp.radius || dist < 1e-4f) continue;
        const float lambert = (core::dot(n, d * (1.0f / dist)) + kWrap) / (1.0f + kWrap);
        if (lambert <= 0.0f) continue;
        const float fall = 1.0f - dist / lamp.radius;
        const float k = lamp.power * lambert * fall * fall / 255.0f;
        light = light + core::Vec3{lamp.color.r * k, lamp.color.g * k, lamp.color.b * k};
    }
    return light;
}

float club_light(const ClubLayout& layout, core::Vec3 p, core::Vec3 n) {
    const core::Vec3 c = club_light_rgb(layout, p, n);
    return (c.x + c.y + c.z) * (1.0f / 3.0f);
}

bool club_room_build(const ClubLayout& layout, render::Vertex* vertices, int vertex_cap, uint16_t* indices,
                     int index_cap, ClubMeshCounts* out) {
    const ClubRoom& r = layout.room;
    const float W = r.size.x, H = r.size.y, D = r.size.z;
    Builder b{layout, vertices, vertex_cap, indices, index_cap};
    const core::Vec3 X{1, 0, 0}, Y{0, 1, 0}, Z{0, 0, 1};
    const float split[] = {r.wall_split};
    const float tile = r.tile;

    // Пол: шахматка линолеума по сетке плиток.
    b.surface({0, 0, 0}, X, Z, Y, W, D, nullptr, 0, nullptr, 0, [&](float a, float c) {
        const int parity = (static_cast<int>(a / tile) + static_cast<int>(c / tile)) & 1;
        return Cell{true, parity ? r.floor_alt : r.floor, true};
    });
    // Потолок.
    b.surface({0, H, 0}, X, Z, Y * -1.0f, W, D, nullptr, 0, nullptr, 0,
              [&](float, float) { return Cell{true, r.ceiling, true}; });
    auto wall = [&](float y) { return Cell{true, y < r.wall_split ? r.wall_low : r.wall_high, true}; };
    // Стена z = 0 с проёмом.
    {
        const float ex[] = {r.door_x0, r.door_x1};
        const float ey[] = {r.wall_split, r.door_height};
        b.surface({0, 0, 0}, X, Y, Z, W, H, ex, 2, ey, 2, [&](float a, float y) {
            if (a > r.door_x0 && a < r.door_x1 && y < r.door_height) return Cell{false, {}, false};
            return wall(y);
        });
    }
    // Стена z = D.
    b.surface({0, 0, D}, X, Y, Z * -1.0f, W, H, nullptr, 0, split, 1, [&](float, float y) { return wall(y); });
    // Стена x = 0.
    b.surface({0, 0, 0}, Z, Y, X, D, H, nullptr, 0, split, 1, [&](float, float y) { return wall(y); });
    // Стена x = W с окном (светится само: день за окном).
    {
        const float ez[] = {r.window_z0, r.window_z1};
        const float ey[] = {r.wall_split, r.window_y0, r.window_y1};
        b.surface({W, 0, 0}, Z, Y, X * -1.0f, D, H, ez, 2, ey, 3, [&](float a, float y) {
            if (a > r.window_z0 && a < r.window_z1 && y > r.window_y0 && y < r.window_y1)
                return Cell{true, r.window, false};
            return wall(y);
        });
    }
    // Тёмный тамбур за проёмом: задник, откосы, перемычка и пол — чтобы не было видно пустоты.
    {
        constexpr float kDepth = 0.5f;
        const float dw = r.door_x1 - r.door_x0, dh = r.door_height;
        const Cell dark{true, r.doorway, false};
        auto flat = [&](float, float) { return dark; };
        b.surface({r.door_x0, 0, -kDepth}, X, Y, Z, dw, dh, nullptr, 0, nullptr, 0, flat);
        b.surface({r.door_x0, 0, -kDepth}, Z, Y, X, kDepth, dh, nullptr, 0, nullptr, 0, flat);
        b.surface({r.door_x1, 0, -kDepth}, Z, Y, X * -1.0f, kDepth, dh, nullptr, 0, nullptr, 0, flat);
        b.surface({r.door_x0, dh, -kDepth}, X, Z, Y * -1.0f, dw, kDepth, nullptr, 0, nullptr, 0, flat);
        b.surface({r.door_x0, 0, -kDepth}, X, Z, Y, dw, kDepth, nullptr, 0, nullptr, 0, flat);
    }
    // Плафоны: на 3 см ниже потолка (без z-конфликта на 16-битном Z PSP).
    for (int i = 0; i < layout.lamp_count; ++i) {
        const ClubLamp& l = layout.lamps[i];
        const core::Vec3 o{l.pos.x - 0.5f * l.size_x, H - 0.03f, l.pos.z - 0.5f * l.size_z};
        b.surface(o, X, Z, Y * -1.0f, l.size_x, l.size_z, nullptr, 0, nullptr, 0,
                  [&](float, float) { return Cell{true, l.color, false}; });
    }

    if (out) *out = {b.vn, b.in};
    return !b.overflow;
}

}  // namespace game
