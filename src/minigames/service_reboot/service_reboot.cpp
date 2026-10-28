#include "minigames/service_reboot/service_reboot.h"

#include "minigames/service_common/service_common.h"

namespace minigame {
namespace {

constexpr float kLimit = 30.0f;
enum class Stage : uint8_t { kPower, kBoot, kMode, kCheck, kVerify };

struct State {
    service::Attempt attempt;
    core::Rng rng;
    Stage stage;
    float progress;
    float feedback;
    int remaining;
    int errors;
    int selected;
    int modes[3];
};
static_assert(sizeof(State) <= kStateBudget, "svc_reboot state budget");

const char* g_hint = "";
const char* g_time = "";
const char* g_power = "";
const char* g_boot = "";
const char* g_mode = "";
const char* g_check = "";
const char* g_verify = "";
const char* g_error = "";
const char* g_modes[3] = {};

bool load() {
    g_hint = str("mg.svc_reboot.hint");
    g_time = str("mg.svc_reboot.time");
    g_power = str("mg.svc_reboot.power");
    g_boot = str("mg.svc_reboot.boot");
    g_mode = str("mg.svc_reboot.mode");
    g_check = str("mg.svc_reboot.check");
    g_verify = str("mg.svc_reboot.verify");
    g_error = str("mg.svc_reboot.error");
    g_modes[0] = str("mg.svc_reboot.safe");
    g_modes[1] = str("mg.svc_reboot.normal");
    g_modes[2] = str("mg.svc_reboot.last");
    return true;
}

void start(void* p, const StartParams& params) {
    State& s = *static_cast<State*>(p);
    s = State{};
    core::rng_seed(s.rng, params.seed);
    s.remaining = 1 + static_cast<int>(2.0f * service::clamp01(params.difficulty));
}

Status update(void* p, float dt, const Input& in) {
    State& s = *static_cast<State*>(p);
    if (service::tick(s.attempt, dt, kLimit)) return Status::kFinished;
    if (s.feedback > 0.0f) s.feedback -= dt;
    switch (s.stage) {
    case Stage::kPower:
        if (in.held & platform::kButtonCross) {
            s.progress += dt;
            if (s.progress >= 2.0f) { s.stage = Stage::kBoot; s.progress = 0.0f; }
        } else {
            if (s.progress >= 0.25f) { ++s.errors; s.feedback = 1.0f; }
            s.progress = 0.0f;
        }
        break;
    case Stage::kBoot:
        s.progress += dt;
        if (s.progress >= 1.0f) {
            for (int i = 0; i < 3; ++i) s.modes[i] = i;
            service::shuffle(s.rng, s.modes, 3);
            s.selected = 0;
            s.progress = 0.0f;
            s.stage = Stage::kMode;
        }
        break;
    case Stage::kMode:
        service::move_menu(s.selected, 3, in);
        if (in.pressed & platform::kButtonCross) {
            if (s.modes[s.selected] == 0) s.stage = Stage::kCheck;
            else { ++s.errors; s.feedback = 1.0f; s.stage = Stage::kPower; }
            s.progress = 0.0f;
        }
        break;
    case Stage::kCheck:
        s.progress += dt;
        if (s.progress >= 1.0f) {
            s.stage = --s.remaining == 0 ? Stage::kVerify : Stage::kPower;
            s.progress = 0.0f;
        }
        break;
    case Stage::kVerify:
        s.progress += dt;
        if (s.progress >= 5.0f)
            return service::finish(s.attempt, service::quality(s.attempt.time, 22.0f,
                                                              kLimit, static_cast<float>(s.errors)));
        break;
    }
    return Status::kRunning;
}

void draw(const void* p, const Canvas& c) {
    const State& s = *static_cast<const State*>(p);
    service::panel(c, g_hint, g_time, s.attempt, kLimit);
    const char* status = g_power;
    float progress = s.progress / 2.0f;
    switch (s.stage) {
    case Stage::kPower: break;
    case Stage::kBoot: status = g_boot; progress = s.progress; break;
    case Stage::kMode:
        status = g_mode;
        for (int i = 0; i < 3; ++i)
            service::menu_item(c, i, g_modes[s.modes[i]], i == s.selected);
        break;
    case Stage::kCheck: status = g_check; progress = s.progress; break;
    case Stage::kVerify: status = g_verify; progress = s.progress / 5.0f; break;
    }
    text(c, 12, 38, status, ui::kColText);
    if (s.stage != Stage::kMode) service::bar(c, 104, progress, ui::kColGood);
    if (s.feedback > 0.0f) text(c, 12, 170, g_error, ui::kColBad);
}

void result(const void* p, Result* out) {
    service::result(static_cast<const State*>(p)->attempt, out);
}

}  // namespace

const Desc& svc_reboot_desc() {
    static const Desc d = [] {
        Desc x;
        x.id = "svc_reboot";
        x.title_key = "mg.svc_reboot.name";
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
