#include "minigames/service_install/service_install.h"

#include "minigames/service_common/service_common.h"

namespace minigame {
namespace {

constexpr float kLimit = 35.0f;
enum class Stage : uint8_t { kPath, kNext, kDisc, kCopy, kVerify };
struct State {
    service::Attempt attempt;
    core::Rng rng;
    Stage stage;
    float progress;
    float feedback;
    int discs;
    int current;
    int selected;
    int count;
    int items[5];
    int errors;
};
static_assert(sizeof(State) <= kStateBudget, "svc_install state budget");

const char* g_hint = "";
const char* g_time = "";
const char* g_path = "";
const char* g_next = "";
const char* g_copy = "";
const char* g_verify = "";
const char* g_error = "";
const char* g_paths[3] = {};
const char* g_actions[2] = {};
const char* g_discs[5] = {};
const char* g_need[5] = {};

bool load() {
    g_hint = str("mg.svc_install.hint");
    g_time = str("mg.svc_install.time");
    g_path = str("mg.svc_install.path");
    g_next = str("mg.svc_install.continue");
    g_copy = str("mg.svc_install.copy");
    g_verify = str("mg.svc_install.verify");
    g_error = str("mg.svc_install.error");
    g_paths[0] = str("mg.svc_install.games");
    g_paths[1] = str("mg.svc_install.system");
    g_paths[2] = str("mg.svc_install.desktop");
    g_actions[0] = str("mg.svc_install.next");
    g_actions[1] = str("mg.svc_install.ad");
    g_discs[0] = str("mg.svc_install.disc_1");
    g_discs[1] = str("mg.svc_install.disc_2");
    g_discs[2] = str("mg.svc_install.disc_3");
    g_discs[3] = str("mg.svc_install.disc_4");
    g_discs[4] = str("mg.svc_install.disc_5");
    g_need[0] = str("mg.svc_install.need_1");
    g_need[1] = str("mg.svc_install.need_2");
    g_need[2] = str("mg.svc_install.need_3");
    g_need[3] = str("mg.svc_install.need_4");
    g_need[4] = str("mg.svc_install.need_5");
    return true;
}

void new_menu(State& s, Stage stage, int count) {
    s.stage = stage;
    s.count = count;
    s.selected = 0;
    for (int i = 0; i < count; ++i) s.items[i] = i;
    service::shuffle(s.rng, s.items, count);
}

void start(void* p, const StartParams& params) {
    State& s = *static_cast<State*>(p);
    s = State{};
    core::rng_seed(s.rng, params.seed);
    s.discs = 3 + static_cast<int>(2.0f * service::clamp01(params.difficulty));
    new_menu(s, Stage::kPath, 3);
}

Status update(void* p, float dt, const Input& in) {
    State& s = *static_cast<State*>(p);
    if (service::tick(s.attempt, dt, kLimit)) return Status::kFinished;
    if (s.feedback > 0.0f) s.feedback -= dt;
    if (s.stage == Stage::kVerify) {
        s.progress += dt;
        if (s.progress >= 2.0f)
            return service::finish(s.attempt, service::quality(s.attempt.time, 24.0f,
                                                              kLimit, static_cast<float>(s.errors)));
    } else if (s.stage == Stage::kCopy) {
        s.progress += dt;
        if (s.progress >= 2.0f) {
            s.progress = 0.0f;
            if (++s.current == s.discs) s.stage = Stage::kVerify;
            else new_menu(s, Stage::kNext, 2);
        }
    } else {
        service::move_menu(s.selected, s.count, in);
        if (in.pressed & platform::kButtonCross) {
            const int correct = s.stage == Stage::kDisc ? s.current : 0;
            if (s.items[s.selected] != correct) { ++s.errors; s.feedback = 1.0f; }
            else if (s.stage == Stage::kPath) new_menu(s, Stage::kNext, 2);
            else if (s.stage == Stage::kNext) new_menu(s, Stage::kDisc, s.discs);
            else { s.stage = Stage::kCopy; s.progress = 0.0f; }
        }
    }
    return Status::kRunning;
}

void draw(const void* p, const Canvas& c) {
    const State& s = *static_cast<const State*>(p);
    service::panel(c, g_hint, g_time, s.attempt, kLimit);
    const char* status = g_verify;
    const char* const* labels = nullptr;
    switch (s.stage) {
    case Stage::kPath: status = g_path; labels = g_paths; break;
    case Stage::kNext: status = g_next; labels = g_actions; break;
    case Stage::kDisc: status = g_need[s.current]; labels = g_discs; break;
    case Stage::kCopy: status = g_copy; break;
    case Stage::kVerify: break;
    }
    text(c, 12, 38, status, ui::kColText);
    if (labels) {
        for (int i = 0; i < s.count; ++i)
            service::menu_item(c, i, labels[s.items[i]], i == s.selected);
    } else service::bar(c, 104, s.progress / 2.0f, ui::kColGood);
    if (s.feedback > 0.0f) text(c, 12, 186, g_error, ui::kColBad);
}

void result(const void* p, Result* out) {
    service::result(static_cast<const State*>(p)->attempt, out);
}

}  // namespace

const Desc& svc_install_desc() {
    static const Desc d = [] {
        Desc x;
        x.id = "svc_install";
        x.title_key = "mg.svc_install.name";
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
