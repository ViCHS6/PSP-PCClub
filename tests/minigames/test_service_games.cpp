// Только Desc и видимый интерфейс: боты не знают устройства состояния игр.
#include <doctest/doctest.h>

#include <array>
#include <cstring>
#include <string>
#include <vector>

#include "minigames/service_clean/service_clean.h"
#include "minigames/service_install/service_install.h"
#include "minigames/service_reboot/service_reboot.h"

namespace {

struct Label {
    std::string key;
    int y;
    render::Color color;
};
struct Rect {
    int x, y, w, h;
    render::Color color;
};
struct View {
    std::vector<Label> labels;
    std::vector<Rect> rects;
};
View g_view;

bool visible(const char* key) {
    for (const Label& label : g_view.labels) if (label.key == key) return true;
    return false;
}

minigame::Input buttons(uint32_t pressed, uint32_t held = 0) {
    minigame::Input in;
    in.pressed = pressed;
    in.held = held | pressed;
    return in;
}

minigame::Input choose(const char* key) {
    for (const Label& label : g_view.labels) {
        if (label.key == key)
            return buttons(label.color == ui::kColTextInv ? platform::kButtonCross :
                                                               platform::kButtonDown);
    }
    FAIL_CHECK("В меню нет требуемого пункта");
    return {};
}

enum class Bot { kIdle, kPerfect, kOneError, kWrongPath, kWrongDisc, kWaste,
                 kPartial, kHold, kMash };

minigame::Input reboot_bot(Bot bot, bool& mistake) {
    if (visible("mg.svc_reboot.power")) return buttons(0, platform::kButtonCross);
    if (visible("mg.svc_reboot.mode")) {
        const char* target = bot == Bot::kOneError && !mistake ?
            "mg.svc_reboot.normal" : "mg.svc_reboot.safe";
        const auto in = choose(target);
        if (in.pressed == platform::kButtonCross && bot == Bot::kOneError) mistake = true;
        return in;
    }
    return {};
}

minigame::Input install_bot(Bot bot, bool& mistake) {
    const char* target = nullptr;
    if (visible("mg.svc_install.path")) target = "mg.svc_install.games";
    if (visible("mg.svc_install.continue")) target = "mg.svc_install.next";
    const char* need[] = {"mg.svc_install.need_1", "mg.svc_install.need_2",
                         "mg.svc_install.need_3", "mg.svc_install.need_4",
                         "mg.svc_install.need_5"};
    const char* disc[] = {"mg.svc_install.disc_1", "mg.svc_install.disc_2",
                         "mg.svc_install.disc_3", "mg.svc_install.disc_4",
                         "mg.svc_install.disc_5"};
    for (int i = 0; i < 5; ++i) if (visible(need[i])) target = disc[i];
    if (!target) return {};
    if (!mistake && ((bot == Bot::kWrongPath && visible("mg.svc_install.path")) ||
                     (bot == Bot::kWrongDisc && visible(need[0])))) {
        const auto in = choose(bot == Bot::kWrongPath ? "mg.svc_install.system" : disc[1]);
        if (in.pressed == platform::kButtonCross) mistake = true;
        return in;
    }
    if (bot == Bot::kOneError && !mistake && visible("mg.svc_install.continue")) {
        const auto in = choose("mg.svc_install.ad");
        if (in.pressed == platform::kButtonCross) mistake = true;
        return in;
    }
    return choose(target);
}

minigame::Input clean_bot(Bot bot, std::array<bool, 12>& cleaned) {
    if (visible("mg.svc_clean.verify")) return {};
    int col = -1, row = -1;
    for (const Rect& rect : g_view.rects) {
        if (rect.w == 96 && rect.h == 32 && rect.color == ui::kColAccent) {
            col = (rect.x - 18) / 104;
            row = (rect.y - 56) / 36;
        }
    }
    if (visible("mg.svc_clean.partial")) return buttons(platform::kButtonCross);
    REQUIRE(col >= 0);
    REQUIRE(row >= 0);
    if (visible("mg.svc_clean.clear")) cleaned[row * 4 + col] = true;
    int count = 0;
    for (bool done : cleaned) if (done) ++count;
    if (bot == Bot::kPartial && count >= 8) return buttons(platform::kButtonDown);
    if (visible("mg.svc_clean.dusty")) return buttons(0, platform::kButtonCross);
    if (row % 2 == 0 && col < 3) return buttons(platform::kButtonRight);
    if (row % 2 == 1 && col > 0) return buttons(platform::kButtonLeft);
    return buttons(platform::kButtonDown);
}

using DescFn = const minigame::Desc& (*)();
const std::array<DescFn, 3> kGames = {minigame::svc_reboot_desc, minigame::svc_clean_desc,
                                   minigame::svc_install_desc};
constexpr float kDt = 1.0f / 30.0f;

struct Run {
    minigame::Result result;
    int frames = 0;
    uint64_t trace = 14695981039346656037ull;
    int work = 0;
};

void hash_bytes(uint64_t& hash, const void* data, size_t count) {
    const auto* bytes = static_cast<const unsigned char*>(data);
    for (size_t i = 0; i < count; ++i) { hash ^= bytes[i]; hash *= 1099511628211ull; }
}

void hash_view(uint64_t& hash) {
    for (const Label& label : g_view.labels) {
        hash_bytes(hash, label.key.data(), label.key.size());
        hash_bytes(hash, &label.y, sizeof(label.y));
        hash_bytes(hash, &label.color, sizeof(label.color));
    }
    for (const Rect& rect : g_view.rects) {
        const int coords[] = {rect.x, rect.y, rect.w, rect.h};
        hash_bytes(hash, coords, sizeof(coords));
        hash_bytes(hash, &rect.color, sizeof(rect.color));
    }
}

Run run(const minigame::Desc& d, uint32_t seed, float difficulty, Bot bot,
        float delay = 0.0f, float dt = kDt) {
    alignas(16) std::array<uint8_t, minigame::kStateBudget + 16> arena;
    arena.fill(0xCD);
    REQUIRE(d.state_size <= minigame::kStateBudget);
    std::memset(arena.data(), 0, d.state_size);
    REQUIRE(d.load());
    minigame::StartParams params;
    params.kind = minigame::kService;
    params.seed = seed;
    params.difficulty = difficulty;
    d.start(arena.data(), params);
    Run out;
    bool mistake = false;
    std::array<bool, 12> cleaned{};
    minigame::Status status = minigame::Status::kRunning;
    int copy_frames = 0;
    int empty_frames = 0;
    const minigame::Canvas canvas{0, 0, d.width, d.height};
    while (status == minigame::Status::kRunning && out.frames * dt < 41.0f) {
        g_view = {};
        d.draw(arena.data(), canvas);
        hash_view(out.trace);
        REQUIRE(g_view.labels.size() + g_view.rects.size() <= 256);
        for (const Rect& r : g_view.rects) {
            CHECK(r.x >= 0);
            CHECK(r.y >= 0);
            CHECK(r.w >= 0);
            CHECK(r.h >= 0);
            CHECK(r.x + r.w <= d.width);
            CHECK(r.y + r.h <= d.height);
        }
        if (visible("mg.svc_install.copy")) ++copy_frames;
        minigame::Input in;
        if (out.frames * dt >= delay) {
            if (bot == Bot::kHold) in = buttons(0, platform::kButtonCross);
            else if (bot == Bot::kMash) {
                const uint32_t dirs[] = {platform::kButtonLeft, platform::kButtonUp,
                                         platform::kButtonRight, platform::kButtonDown};
                in = buttons(dirs[(out.frames / 7) % 4] | platform::kButtonCross);
            } else if (bot != Bot::kIdle) {
                if (std::strcmp(d.id, "svc_reboot") == 0) in = reboot_bot(bot, mistake);
                else if (std::strcmp(d.id, "svc_install") == 0) in = install_bot(bot, mistake);
                else if (bot == Bot::kWaste && visible("mg.svc_clean.clear") && empty_frames < 30) {
                    in = buttons(0, platform::kButtonCross);
                    ++empty_frames;
                } else in = clean_bot(bot, cleaned);
            }
        }
        status = d.update(arena.data(), dt, in);
        ++out.frames;
    }
    REQUIRE(status == minigame::Status::kFinished);
    d.result(arena.data(), &out.result);
    CHECK(out.result.quality >= 0.0f);
    CHECK(out.result.quality <= 1.0f);
    CHECK(out.result.score == 0);
    CHECK(out.frames * dt >= 10.0f - dt);
    CHECK(out.frames * dt <= 40.0f);
    bool intact = true;
    for (size_t i = d.state_size; i < arena.size(); ++i) if (arena[i] != 0xCD) intact = false;
    CHECK(intact);
    // Завершённая попытка неизменна, даже если кто-то прислал лишний кадр.
    CHECK(d.update(arena.data(), dt, buttons(platform::kButtonCross)) == minigame::Status::kFinished);
    minigame::Result again;
    d.result(arena.data(), &again);
    CHECK(again.outcome == out.result.outcome);
    CHECK(again.quality == out.result.quality);
    out.work = copy_frames;
    return out;
}

}  // namespace

// SDK заменён записью команд рисования; ни хоста, ни графического устройства.
namespace ui {
const char* str(const char* key) { return key; }
}  // namespace ui
namespace minigame {
void fill(const Canvas&, int x, int y, int w, int h, render::Color color) {
    g_view.rects.push_back({x, y, w, h, color});
}
int text(const Canvas& c, int x, int y, const char* utf8, render::Color color, ui::Font, int) {
    CHECK(x >= 0);
    CHECK(y >= 0);
    CHECK(x < c.w);
    CHECK(y + 12 <= c.h);
    g_view.labels.push_back({utf8, y, color});
    return 0;
}
}  // namespace minigame

TEST_CASE("service: descriptor limits and read-only draw") {
    const char* ids[] = {"svc_reboot", "svc_clean", "svc_install"};
    for (size_t i = 0; i < kGames.size(); ++i) {
        const auto& d = kGames[i]();
        CHECK(std::strcmp(d.id, ids[i]) == 0);
        CHECK(d.kinds == minigame::kService);
        CHECK(d.width <= minigame::kMaxCanvasWidth);
        CHECK(d.height <= minigame::kMaxCanvasHeight);
        CHECK(d.state_size > 0);
        REQUIRE(d.state_size <= minigame::kStateBudget);
        REQUIRE(d.load());
        alignas(16) std::array<uint8_t, minigame::kStateBudget> arena{};
        minigame::StartParams p;
        p.kind = minigame::kService;
        d.start(arena.data(), p);
        const auto before = arena;
        g_view = {};
        d.draw(arena.data(), {0, 0, d.width, d.height});
        CHECK(arena == before);
    }
}

TEST_CASE("service: ideal visible-interface bots complete with quality one in 10-40 seconds") {
    for (auto get : kGames) {
        const auto& d = get();
        CAPTURE(d.id);
        for (float difficulty : {0.0f, 0.5f, 1.0f}) {
            CAPTURE(difficulty);
            for (uint32_t seed : {0u, 1u, 7u, 42u, 123456u, 0xFFFFFFFFu}) {
                CAPTURE(seed);
                const Run a = run(d, seed, difficulty, Bot::kPerfect);
                CHECK(a.result.outcome == minigame::Outcome::kCompleted);
                CHECK(a.result.quality == doctest::Approx(1.0f));
            }
        }
    }
}

TEST_CASE("service: no input and a held cross cannot bypass the work, timeout fails") {
    for (auto get : kGames) {
        const auto& d = get();
        CAPTURE(d.id);
        for (float dt : {1.0f / 60.0f, 1.0f / 30.0f, 0.1f}) {
            for (Bot bot : {Bot::kIdle, Bot::kHold}) {
                const Run a = run(d, 19, 1.0f, bot, 0.0f, dt);
                CHECK(a.result.outcome == minigame::Outcome::kFailed);
                CHECK(a.result.quality == 0.0f);
                CHECK(a.frames * dt >= 10.0f);
                CHECK(a.frames * dt <= 40.0f);
            }
        }
    }
}

TEST_CASE("service: same seed and inputs reproduce the visible attempt and outcome") {
    for (auto get : kGames) {
        const auto& d = get();
        CAPTURE(d.id);
        for (Bot bot : {Bot::kPerfect, Bot::kMash}) {
            const Run a = run(d, 1234, 0.5f, bot);
            const Run b = run(d, 1234, 0.5f, bot);
            CHECK(a.trace == b.trace);
            CHECK(a.frames == b.frames);
            CHECK(a.result.outcome == b.result.outcome);
            CHECK(a.result.quality == b.result.quality);
        }
        const Run a = run(d, 1, 0.5f, Bot::kPerfect);
        const Run b = run(d, 42, 0.5f, Bot::kPerfect);
        CHECK(a.trace != b.trace);
    }
}

TEST_CASE("service: recoverable menu mistakes lower quality without preventing completion") {
    for (auto get : {minigame::svc_reboot_desc, minigame::svc_install_desc}) {
        const auto& d = get();
        CAPTURE(d.id);
        const Run a = run(d, 77, 0.5f, Bot::kOneError);
        CHECK(a.result.outcome == minigame::Outcome::kCompleted);
        CHECK(a.result.quality >= 0.6f);
        CHECK(a.result.quality <= 0.9f);
    }
    for (Bot bot : {Bot::kWrongPath, Bot::kWrongDisc}) {
        const Run a = run(minigame::svc_install_desc(), 77, 0.5f, bot);
        CHECK(a.result.outcome == minigame::Outcome::kCompleted);
        CHECK(a.result.quality >= 0.6f);
        CHECK(a.result.quality <= 0.9f);
    }
}

TEST_CASE("service: late success scores less, but deadline always fails") {
    for (auto get : kGames) {
        const auto& d = get();
        CAPTURE(d.id);
        const float delay = std::strcmp(d.id, "svc_reboot") == 0 ? 19.0f :
                            (std::strcmp(d.id, "svc_clean") == 0 ? 28.0f : 25.0f);
        const Run a = run(d, 7, 0.0f, Bot::kPerfect, delay);
        CHECK(a.result.outcome == minigame::Outcome::kCompleted);
        CHECK(a.result.quality > 0.0f);
        CHECK(a.result.quality < 0.6f);
        const Run b = run(d, 7, 0.0f, Bot::kPerfect, 34.0f);
        CHECK(b.result.outcome == minigame::Outcome::kFailed);
        CHECK(b.result.quality == 0.0f);
    }
}

TEST_CASE("service: difficulty increases work and keeps ideal quality") {
    for (auto get : kGames) {
        const auto& d = get();
        CAPTURE(d.id);
        const Run easy = run(d, 11, 0.0f, Bot::kPerfect);
        const Run hard = run(d, 11, 1.0f, Bot::kPerfect);
        CHECK(hard.frames > easy.frames);
        CHECK(easy.result.quality == hard.result.quality);
        if (std::strcmp(d.id, "svc_install") == 0) CHECK(hard.work > easy.work);
    }
}

TEST_CASE("service: out-of-range difficulty is clamped") {
    for (auto get : kGames) {
        const auto& d = get();
        CAPTURE(d.id);
        CHECK(run(d, 8, -1.0f, Bot::kPerfect).trace == run(d, 8, 0.0f, Bot::kPerfect).trace);
        CHECK(run(d, 8, 2.0f, Bot::kPerfect).trace == run(d, 8, 1.0f, Bot::kPerfect).trace);
    }
}

TEST_CASE("service clean: partial cleaning can finish, empty cleaning fails") {
    const auto& d = minigame::svc_clean_desc();
    const Run partial = run(d, 23, 1.0f, Bot::kPartial);
    CHECK(partial.result.outcome == minigame::Outcome::kCompleted);
    CHECK(partial.result.quality >= 0.5f);
    CHECK(partial.result.quality < 0.9f);

    REQUIRE(d.load());
    alignas(16) std::array<uint8_t, minigame::kStateBudget> arena{};
    minigame::StartParams params;
    d.start(arena.data(), params);
    for (int i = 0; i < 3; ++i) d.update(arena.data(), kDt, buttons(platform::kButtonDown));
    d.update(arena.data(), kDt, buttons(platform::kButtonCross));
    int frames = 4;
    while (frames < 1200 && d.update(arena.data(), kDt, {}) == minigame::Status::kRunning) ++frames;
    minigame::Result result;
    d.result(arena.data(), &result);
    CHECK(result.outcome == minigame::Outcome::kFailed);
    CHECK(result.quality == 0.0f);
    CHECK(frames * kDt >= 10.0f - kDt);
    CHECK(frames * kDt <= 40.0f);
}

TEST_CASE("service clean: brushing a clean zone wastes quality") {
    const Run a = run(minigame::svc_clean_desc(), 4, 0.5f, Bot::kWaste);
    CHECK(a.result.outcome == minigame::Outcome::kCompleted);
    CHECK(a.result.quality >= 0.6f);
    CHECK(a.result.quality <= 0.9f);
}

TEST_CASE("service reboot: interrupted power hold resets progress and reduces quality") {
    const auto& d = minigame::svc_reboot_desc();
    REQUIRE(d.load());
    alignas(16) std::array<uint8_t, minigame::kStateBudget> arena{};
    minigame::StartParams params;
    params.kind = minigame::kService;
    params.difficulty = 0.0f;
    d.start(arena.data(), params);
    for (int i = 0; i < 30; ++i)
        CHECK(d.update(arena.data(), kDt, buttons(0, platform::kButtonCross)) == minigame::Status::kRunning);
    d.update(arena.data(), kDt, {});
    // После отпускания короткое удержание не должно закончить двухсекундный шаг.
    for (int i = 0; i < 45; ++i)
        d.update(arena.data(), kDt, buttons(0, platform::kButtonCross));
    g_view = {};
    d.draw(arena.data(), {0, 0, d.width, d.height});
    CHECK(visible("mg.svc_reboot.power"));
    bool mistake = false;
    minigame::Status status = minigame::Status::kRunning;
    int frames = 76;
    while (status == minigame::Status::kRunning && frames < 1200) {
        g_view = {};
        d.draw(arena.data(), {0, 0, d.width, d.height});
        status = d.update(arena.data(), kDt, reboot_bot(Bot::kPerfect, mistake));
        ++frames;
    }
    CHECK(status == minigame::Status::kFinished);
    minigame::Result result;
    d.result(arena.data(), &result);
    CHECK(result.outcome == minigame::Outcome::kCompleted);
    CHECK(result.quality >= 0.6f);
    CHECK(result.quality <= 0.9f);
    CHECK(frames * kDt >= 10.0f);
    CHECK(frames * kDt <= 40.0f);
}

TEST_CASE("service: visible time bar shrinks while idle") {
    for (auto get : kGames) {
        const auto& d = get();
        REQUIRE(d.load());
        alignas(16) std::array<uint8_t, minigame::kStateBudget> arena{};
        d.start(arena.data(), {});
        const minigame::Canvas canvas{0, 0, d.width, d.height};
        auto bar_width = [&] {
            g_view = {};
            d.draw(arena.data(), canvas);
            int width = -1;
            for (const Rect& r : g_view.rects)
                if (r.y == 224 && r.color == ui::kColWarn) width = r.w;
            return width;
        };
        const int initial = bar_width();
        for (int i = 0; i < 150; ++i) d.update(arena.data(), kDt, {});
        const int later = bar_width();
        CHECK(initial > later);
        CHECK(later > 0);
    }
}

TEST_CASE("service clean: joystick and D-pad stay inside the grid") {
    const auto& d = minigame::svc_clean_desc();
    REQUIRE(d.load());
    alignas(16) std::array<uint8_t, minigame::kStateBudget> arena{};
    d.start(arena.data(), {});
    for (int frame = 0; frame < 300; ++frame) {
        minigame::Input in;
        in.stick_x = frame < 150 ? -1.0f : 1.0f;
        in.stick_y = -1.0f;
        d.update(arena.data(), kDt, in);
    }
    g_view = {};
    d.draw(arena.data(), {0, 0, d.width, d.height});
    bool selected = false;
    for (const Rect& r : g_view.rects) {
        if (r.w == 96 && r.h == 32 && r.color == ui::kColAccent) {
            CHECK(r.x == 18 + 3 * 104);
            CHECK(r.y == 56);
            selected = true;
        }
    }
    CHECK(selected);
}
