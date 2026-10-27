#include <doctest/doctest.h>
#include <limits>

#include "render/mesh_format.h"
#include "render/texture_format.h"
#include "fixtures.h"

using namespace render_test;
namespace fmt = render::format;

namespace {

void bad_mesh(const Bytes& b) {
    fmt::MeshView view;
    const char* error = nullptr;
    CHECK_FALSE(fmt::validate_mesh(b.data(), b.size(), &view, &error));
    CHECK(view.data == nullptr);
    CHECK(error != nullptr);
}

void bad_texture(const Bytes& b) {
    fmt::TextureView view;
    const char* error = nullptr;
    CHECK_FALSE(fmt::validate_texture(b.data(), b.size(), &view, &error));
    CHECK(view.data == nullptr);
    CHECK(error != nullptr);
}

}  // namespace

TEST_CASE("MSH1 numeric quad is 220 bytes and decodes unsigned tiled UV") {
    auto b = quad();
    fmt::MeshView m;
    REQUIRE(fmt::validate_mesh(b.data(), b.size(), &m, nullptr));
    CHECK(b.size() == 220);
    CHECK(m.h.vertices_offset == 144);
    CHECK(m.h.indices_offset == 208);
    CHECK(m.h.vertex_count == 4);
    CHECK(m.h.submesh_count == 1);
    CHECK(m.h.index_count == 6);
    const float positions[4][3] = {{0, 0, 0}, {1.9999694824f, 0, 0},
                                  {1.9999694824f, 1.9999694824f, 0}, {0, 1.9999694824f, 0}};
    const float uv[4][2] = {{-1, 1}, {2, 1}, {2, 0}, {-1, 0}};
    for (uint32_t i = 0; i < 4; ++i) {
        fmt::DecodedVertex v;
        REQUIRE(fmt::decode_vertex(m, i, &v));
        CHECK(v.x == doctest::Approx(positions[i][0]));
        CHECK(v.y == doctest::Approx(positions[i][1]));
        CHECK(v.z == positions[i][2]);
        CHECK(v.u == doctest::Approx(uv[i][0]));
        CHECK(v.v == doctest::Approx(uv[i][1]));
        CHECK(v.color == 0xFF404040);
    }
    fmt::MeshSubset s;
    REQUIRE(fmt::mesh_subset(m, 0, &s));
    CHECK(s.index_start == 0);
    CHECK(s.index_count == 6);
    CHECK(s.texture[0] == 0);
    CHECK_FALSE(fmt::mesh_subset(m, 1, &s));
    fmt::DecodedVertex v;
    CHECK_FALSE(fmt::decode_vertex(m, 4, &v));
    // Вход может быть невыровненным: LE-чтение безопасно, GE получает копию арены.
    b.insert(b.begin(), 0);
    CHECK(fmt::validate_mesh(b.data() + 1, b.size() - 1, &m, nullptr));
}

TEST_CASE("MSH1 rejects truncated files, magic, versions, flags and counts") {
    const auto original = quad();
    for (size_t size = 0; size < original.size(); ++size)
        bad_mesh(Bytes(original.begin(), original.begin() + size));
    CHECK_FALSE(fmt::validate_mesh(nullptr, 96, nullptr, nullptr));
    for (size_t at : {size_t(0), size_t(4), size_t(6), size_t(28)}) {
        auto b = original; b[at] ^= 0x80; bad_mesh(b);
    }
    for (size_t at : {size_t(8), size_t(10)}) {
        auto b = original; u16(b, at, 0); bad_mesh(b);
        u16(b, at, 65535); bad_mesh(b);
    }
    for (uint32_t count : {0u, 5u, 0xFFFFFFFFu, 0xFFFFFFFCu}) {
        auto b = original; u32(b, 12, count); bad_mesh(b);
    }
}

TEST_CASE("MSH1 requires exact offsets, size and zero vertex/file padding") {
    for (size_t at : {size_t(16), size_t(20), size_t(24)}) {
        auto b = quad(); u32(b, at, 0xFFFFFFF0); bad_mesh(b);
        b = quad(); ++b[at]; bad_mesh(b);
    }
    auto b = quad(); b.push_back(0); bad_mesh(b);
    b = quad(); b[158] = 1; bad_mesh(b);
    b = quad(); u32(b, 12, 3); u32(b, 100, 3); b.resize(216);
    b[214] = b[215] = 0;
    CHECK(fmt::validate_mesh(b.data(), b.size(), nullptr, nullptr));
    b[215] = 1; bad_mesh(b);
}

TEST_CASE("MSH1 requires finite coefficients positive scales and ordered bounds") {
    for (size_t at = 32; at < 96; at += 4) {
        auto b = quad(); f32(b, at, std::numeric_limits<float>::infinity()); bad_mesh(b);
        f32(b, at, std::numeric_limits<float>::quiet_NaN()); bad_mesh(b);
    }
    for (size_t at : {size_t(44), size_t(48), size_t(52), size_t(64), size_t(68)}) {
        auto b = quad(); f32(b, at, 0.0f); bad_mesh(b);
        f32(b, at, -1.0f); bad_mesh(b);
    }
    for (size_t a = 0; a < 3; ++a) {
        auto b = quad(); f32(b, 72 + a * 4, 3.0f); bad_mesh(b);
    }
    auto b = quad();
    f32(b, 32, std::numeric_limits<float>::max());
    f32(b, 44, std::numeric_limits<float>::max());
    bad_mesh(b);
}

TEST_CASE("MSH1 subsets partition indices and have safe ASCII texture ids") {
    for (size_t at : {size_t(96), size_t(136), size_t(140)}) {
        auto b = quad(); u32(b, at, 1); bad_mesh(b);
    }
    for (uint32_t count : {0u, 3u, 5u, 9u, 0xFFFFFFFCu}) {
        auto b = quad(); u32(b, 100, count); bad_mesh(b);
    }
    auto b = quad(); std::memset(b.data() + 104, 'a', 32); bad_mesh(b);
    b = quad(); b[104] = 128; bad_mesh(b);
    b = quad(); b[105] = 'a'; bad_mesh(b);
    b = quad(); std::memcpy(b.data() + 104, "tex_desk", 8);
    CHECK(fmt::validate_mesh(b.data(), b.size(), nullptr, nullptr));
    b[104] = '/'; bad_mesh(b);
}

TEST_CASE("MSH1 indices must address existing vertices") {
    for (uint16_t value : {uint16_t(4), uint16_t(65535)}) {
        auto b = quad(); u16(b, 218, value); bad_mesh(b);
    }
}

TEST_CASE("MSH1 multiple subsets and counts above GE command limit") {
    auto b = split_quad();
    REQUIRE(fmt::validate_mesh(b.data(), b.size(), nullptr, nullptr));
    u32(b, 144, 0); bad_mesh(b);
    b = split_quad(); u32(b, 144, 6); bad_mesh(b);
    b = split_quad(); u32(b, 148, 0); bad_mesh(b);
    b = quad();
    constexpr uint32_t count = 65538;
    b.resize(208 + count * 2);
    u32(b, 12, count); u32(b, 100, count);
    for (uint32_t i = 0; i < count; ++i) u16(b, 208 + i * 2, static_cast<uint16_t>(i % 4));
    CHECK(fmt::validate_mesh(b.data(), b.size(), nullptr, nullptr));
}

TEST_CASE("TEX1 RGBA8888 8x16 swizzle example and all decoded pixels") {
    auto b = texture();
    const size_t offsets[] = {0, 16, 112, 128, 256, 384};
    const uint8_t xy[][2] = {{0,0}, {0,1}, {0,7}, {4,0}, {0,8}, {4,8}};
    for (size_t i = 0; i < 6; ++i) {
        CHECK(b[32 + offsets[i]] == xy[i][0]);
        CHECK(b[32 + offsets[i] + 1] == xy[i][1]);
        CHECK(fmt::texture_byte_offset(xy[i][0] * 4, xy[i][1], 32, true) == offsets[i]);
    }
    fmt::TextureView t;
    REQUIRE(fmt::validate_texture(b.data(), b.size(), &t, nullptr));
    CHECK(t.h.frame_bytes == 512);
    CHECK(t.stride_bytes == 32);
    Bytes rgba(512);
    REQUIRE(fmt::decode_texture_frame(t, 0, rgba.data(), rgba.size()));
    for (size_t y = 0; y < 16; ++y) for (size_t x = 0; x < 8; ++x) {
        CHECK(rgba[(y * 8 + x) * 4] == x);
        CHECK(rgba[(y * 8 + x) * 4 + 1] == y);
        CHECK(rgba[(y * 8 + x) * 4 + 3] == 255);
    }
    CHECK_FALSE(fmt::decode_texture_frame(t, 1, rgba.data(), rgba.size()));
    CHECK_FALSE(fmt::decode_texture_frame(t, 0, rgba.data(), rgba.size() - 1));
}

TEST_CASE("TEX1 narrow T4 uses low nibble first and excludes row padding") {
    for (bool swizzle : {false, true}) {
        auto b = texture(4, 8, 8, 1, swizzle);
        for (uint32_t i = 1; i <= 4; ++i) u32(b, 32 + i * 4, 0xFF000000 | i);
        b[96] = 0x21; b[97] = 0x43;
        fmt::TextureView t;
        REQUIRE(fmt::validate_texture(b.data(), b.size(), &t, nullptr));
        CHECK(t.h.stride == 32);
        CHECK(t.h.frame_bytes == 128);
        Bytes rgba(8 * 8 * 4 + 4, 0xCC);
        REQUIRE(fmt::decode_texture_frame(t, 0, rgba.data(), rgba.size() - 4));
        for (size_t x = 0; x < 4; ++x) CHECK(rgba[x * 4] == x + 1);
        CHECK(rgba[8 * 8 * 4] == 0xCC);
        b[96 + 4] = 1; bad_texture(b);
    }
}

TEST_CASE("TEX1 all six formats linear and swizzled support multiple frames") {
    for (uint8_t format = 0; format < 6; ++format) for (bool swizzle : {false, true}) {
        auto b = texture(format, 8, 8, 2, swizzle);
        fmt::TextureView t;
        REQUIRE(fmt::validate_texture(b.data(), b.size(), &t, nullptr));
        Bytes rgba(256);
        REQUIRE(fmt::decode_texture_frame(t, 1, rgba.data(), rgba.size()));
        if (format == 3) {
            CHECK(rgba[0] == 0); CHECK(rgba[2] == 1);
        } else {
            CHECK(rgba[0] == 255); CHECK(rgba[1] == 0); CHECK(rgba[2] == 0);
        }
        CHECK(rgba[3] == 255);
    }
}

TEST_CASE("TEX1 packed channel expansion matches GU bit order") {
    const uint16_t words[] = {0x07E0, 0xF800, 0x0821};
    const uint8_t expected[][3] = {{0,255,0}, {0,0,255}, {8,4,8}};
    for (size_t i = 0; i < 3; ++i) {
        auto b = texture(0, 8, 8, 1, false);
        u16(b, 32, words[i]);
        fmt::TextureView t;
        REQUIRE(fmt::validate_texture(b.data(), b.size(), &t, nullptr));
        Bytes rgba(256);
        REQUIRE(fmt::decode_texture_frame(t, 0, rgba.data(), rgba.size()));
        for (size_t channel = 0; channel < 3; ++channel) CHECK(rgba[channel] == expected[i][channel]);
    }
    for (uint8_t f : {uint8_t(1), uint8_t(2)}) {
        auto b = texture(f, 8, 8, 1, false);
        u16(b, 32, f == 1 ? 0x8421 : 0xF321);
        fmt::TextureView t;
        REQUIRE(fmt::validate_texture(b.data(), b.size(), &t, nullptr));
        Bytes rgba(256);
        REQUIRE(fmt::decode_texture_frame(t, 0, rgba.data(), rgba.size()));
        CHECK(rgba[0] == (f == 1 ? 8 : 17));
        CHECK(rgba[1] == (f == 1 ? 8 : 34));
        CHECK(rgba[2] == (f == 1 ? 8 : 51));
    }
}

TEST_CASE("TEX1 rejects truncated headers and files and invalid header groups") {
    const auto original = texture();
    for (size_t size = 0; size < original.size(); ++size)
        bad_texture(Bytes(original.begin(), original.begin() + size));
    CHECK_FALSE(fmt::validate_texture(nullptr, 32, nullptr, nullptr));
    for (size_t at : {size_t(0), size_t(4), size_t(6), size_t(7), size_t(18)}) {
        auto b = original; b[at] ^= 128; bad_texture(b);
    }
    for (size_t at : {size_t(8), size_t(10)}) for (uint16_t v : {uint16_t(0), uint16_t(4), uint16_t(12), uint16_t(1024)}) {
        auto b = original; u16(b, at, v); bad_texture(b);
    }
    auto b = original; u16(b, 14, 0); bad_texture(b);
    b = original; u16(b, 14, 65535); bad_texture(b);
}

TEST_CASE("TEX1 requires exact stride frame size CLUT counts offsets and size") {
    for (uint8_t f = 0; f < 6; ++f) {
        const auto original = texture(f, 8, 8);
        for (size_t at : {size_t(12), size_t(16), size_t(20), size_t(24), size_t(28)}) {
            auto b = original; ++b[at]; bad_texture(b);
        }
        for (size_t at : {size_t(20), size_t(24), size_t(28)}) {
            auto b = original; u32(b, at, 0xFFFFFFFF); bad_texture(b);
        }
        auto b = original; b.push_back(0); bad_texture(b);
    }
}

TEST_CASE("TEX1 direct alpha is binary and transparency flag reflects all frames") {
    for (uint8_t f : {uint8_t(1), uint8_t(2), uint8_t(3)}) {
        auto b = texture(f, 8, 8, 2, false);
        const size_t frame_bytes = f == 3 ? 256 : 128;
        const size_t at = 32 + frame_bytes;
        if (f == 3) { b[at] = 255; b[at + 3] = 0; }
        else if (f == 2) u16(b, at, 0x000F);
        else u16(b, at, 0x001F);
        bad_texture(b);
        b[7] |= 2;
        fmt::TextureView t;
        REQUIRE(fmt::validate_texture(b.data(), b.size(), &t, nullptr));
        Bytes rgba(256);
        REQUIRE(fmt::decode_texture_frame(t, 1, rgba.data(), rgba.size()));
        CHECK(rgba[3] == 0);
        CHECK(rgba[0] == 255);
        if (f == 3) { b[at + 3] = 128; bad_texture(b); }
        if (f == 2) { u16(b, at, 0x800F); bad_texture(b); }
    }
    for (uint8_t f = 0; f < 6; ++f) {
        auto b = texture(f); b[7] |= 2; bad_texture(b);
    }
}

TEST_CASE("TEX1 CLUT alpha zero colors slot zero and active transparent indices") {
    for (uint8_t f : {uint8_t(4), uint8_t(5)}) {
        auto original = texture(f, 8, 8);
        auto b = original; u32(b, 40, 0x80000000); bad_texture(b);
        b = original; u32(b, 40, 0x000000FF); bad_texture(b);
        b = original;
        const size_t pixels = f == 4 ? 96 : 1056;
        b[pixels] = 0;
        bad_texture(b);
        b[7] |= 2;
        CHECK(fmt::validate_texture(b.data(), b.size(), nullptr, nullptr));
        u32(b, 32, 0xFF000000); bad_texture(b);
        b = original; b[7] |= 2; b[pixels] = f == 4 ? 0x22 : 2; bad_texture(b);
        // Неиспользуемые нулевые слоты, включая CLUT[0], не делают файл прозрачным.
        CHECK(fmt::validate_texture(original.data(), original.size(), nullptr, nullptr));
    }
}

TEST_CASE("TEX1 padding is checked on each frame both layouts") {
    for (bool swizzle : {false, true}) for (uint8_t f : {uint8_t(4), uint8_t(5)}) {
        auto b = texture(f, 8, 8, 2, swizzle);
        const size_t pixels = f == 4 ? 96 : 1056;
        const size_t active = f == 4 ? 4 : 8;
        b[pixels + 128 + offset(active, 7, 16, swizzle)] = 1;
        bad_texture(b);
    }
}
