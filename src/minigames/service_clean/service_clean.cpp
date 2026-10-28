#include "minigames/service_clean/service_clean.h"

#include "minigames/service_common/service_common.h"

namespace minigame {
namespace {

constexpr float kLimit = 35.0f;
constexpr int kColumns = 4;
constexpr int kRows = 3;
constexpr int kZones = kColumns * kRows;
struct State {
    service::Attempt attempt;
    core::Rng rng;
    float dust[kZones];
    float initial[kZones];
    float total;
    float removed;
    float wasted;
    float stick_repeat;
    int col;
    int row;
    bool closing;
};
static_assert(sizeof(State) <= kStateBudget, "svc_clean state budget");

const char* g_hint = "";
const char* g_time = "";
const char* g_dusty = "";
const char* g_clear = "";
const char* g_finish = "";
const char* g_verify = "";
const char* g_partial = "";

bool load() {
    g_hint = str("mg.svc_clean.hint");
    g_time = str("mg.svc_clean.time");
    g_dusty = str("mg.svc_clean.dusty");
    g_clear = str("mg.svc_clean.clear");
    g_finish = str("mg.svc_clean.finish");
    g_verify = str("mg.svc_clean.verify");
    g_partial = str("mg.svc_clean.partial");
    return true;
}

void start(void* p, const StartParams& params) {
    State& s = *static_cast<State*>(p);
    s = State{};
    core::rng_seed(s.rng, params.seed);
    const float thickness = 0.3f + 0.7f * service::clamp01(params.difficulty);
    for (int i = 0; i < kZones; ++i) {
        s.initial[i] = s.dust[i] = thickness * core::rng_range_f(s.rng, 0.8f, 1.2f);
        s.total += s.dust[i];
    }
}

Status update(void* p, float dt, const Input& in) {
    State& s = *static_cast<State*>(p);
    if (service::tick(s.attempt, dt, kLimit)) return Status::kFinished;
    if (s.closing) {
        const float fraction = service::clamp01(s.removed / s.total);
        return service::finish(s.attempt, fraction >= 0.5f ? fraction *
            service::quality(s.attempt.time, 25.0f, kLimit, s.wasted) : 0.0f);
    }

    uint32_t move = in.pressed;
    s.stick_repeat -= dt;
    const bool stick = in.stick_x > 0.5f || in.stick_x < -0.5f ||
                       in.stick_y > 0.5f || in.stick_y < -0.5f;
    if (!stick) s.stick_repeat = 0.0f;
    else if (s.stick_repeat <= 0.0f) {
        if (in.stick_x > 0.5f) move |= platform::kButtonRight;
        else if (in.stick_x < -0.5f) move |= platform::kButtonLeft;
        else if (in.stick_y > 0.5f) move |= platform::kButtonUp;
        else move |= platform::kButtonDown;
        s.stick_repeat = 0.18f;
    }
    if ((move & platform::kButtonLeft) && s.col > 0) --s.col;
    else if ((move & platform::kButtonRight) && s.col < kColumns - 1) ++s.col;
    else if ((move & platform::kButtonUp) && s.row > 0) --s.row;
    else if ((move & platform::kButtonDown) && s.row < kRows) ++s.row;

    if (s.row == kRows) {
        if (in.pressed & platform::kButtonCross) s.closing = true;
    } else if (in.held & platform::kButtonCross) {
        float& dust = s.dust[s.row * kColumns + s.col];
        if (dust > 0.0f) {
            const float amount = dust < dt ? dust : dt;
            s.removed += amount;
            dust -= amount;
        } else s.wasted += dt;
    }
    bool all_clear = true;
    for (float dust : s.dust) if (dust > 0.0f) all_clear = false;
    if (all_clear) { s.removed = s.total; s.closing = true; }
    return Status::kRunning;
}

void draw(const void* p, const Canvas& c) {
    const State& s = *static_cast<const State*>(p);
    service::panel(c, g_hint, g_time, s.attempt, kLimit);
    const int selected = s.row * kColumns + s.col;
    const char* status = s.row == kRows ? g_partial :
                         (s.dust[selected] > 0.0f ? g_dusty : g_clear);
    text(c, 12, 34, s.closing ? g_verify : status, ui::kColText);
    for (int i = 0; i < kZones; ++i) {
        const int x = 20 + (i % kColumns) * 104;
        const int y = 58 + (i / kColumns) * 36;
        fill(c, x - 2, y - 2, 96, 32,
             !s.closing && i == selected ? ui::kColAccent : ui::kColShadow);
        fill(c, x, y, 92, 28, ui::kColGood);
        const float left = s.dust[i] / s.initial[i];
        fill(c, x, y, static_cast<int>(92.0f * left), 28, ui::kColWarn);
    }
    if (s.row == kRows) fill(c, 12, 170, service::kWidth - 24, 24, ui::kColAccent);
    text(c, 20, 176, g_finish, s.row == kRows ? ui::kColTextInv : ui::kColText);
}

void result(const void* p, Result* out) {
    service::result(static_cast<const State*>(p)->attempt, out);
}

}  // namespace

const Desc& svc_clean_desc() {
    static const Desc d = [] {
        Desc x;
        x.id = "svc_clean";
        x.title_key = "mg.svc_clean.name";
        x.kinds = kService;
        x.width = service::kWidth;
        x.height = service::kHeight;
        x.state_size = sizeof(State);
        x.load = load; x.start = start; x.update = update; x.draw = draw; x.result = result;
        return x;
    }();
    return d;
}
}  // namespace minigame
