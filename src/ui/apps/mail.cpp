#include "ui/apps/mail.h"

#include <cstdio>
#include <cstring>

namespace ui {
namespace {
enum : WidgetId { kInbox = 1, kOpen, kBody, kChoice, kAnswer, kBack };
struct State {
    ListState inbox, body;
    const core::World* owner = nullptr;
    const core::GameData* data = nullptr;
    core::Mail opened_mail{};
    int opened = -1;
    int answer = 0;
    const char* kinds[5]{};
    const char* empty = nullptr;
    const char* unavailable = nullptr;
    const char* open = nullptr;
    const char* back = nullptr;
    const char* reply = nullptr;
    const char* answered = nullptr;
    const char* expired = nullptr;
    const char* failed = nullptr;
    const char* row_fmt = nullptr;
    const char* unread = nullptr;
    const char* read = nullptr;
    const char* locked_fmt = nullptr;
    const char* report_fmt = nullptr;
    const char* bill_fmt = nullptr;
    const char* delivery = nullptr;
    const char* news = nullptr;
    const char* no_news = nullptr;
    const char* date_fmt = nullptr;
    const char* status = nullptr;
    char body_text[256]{};
} g_state;

// Строки режем по границам UTF-8; пробел даёт перенос по слову.
const char* next_line(const char* text, char* buf, int cap) {
    int n = 0, space = -1;
    while (text[n] && text[n] != '\n') {
        const unsigned char c = static_cast<unsigned char>(text[n]);
        const int bytes = c < 0x80 ? 1 : c < 0xE0 ? 2 : c < 0xF0 ? 3 : 4;
        if (n + bytes >= cap) break;
        std::memcpy(buf + n, text + n, static_cast<size_t>(bytes));
        buf[n + bytes] = '\0';
        if (text_width(buf) > 450 && n > 0) break;
        if (text[n] == ' ') space = n;
        n += bytes;
    }
    if (text[n] && text[n] != '\n' && space > 0) n = space;
    buf[n] = '\0';
    const char* end = text + n;
    if (*end == '\n' || *end == ' ') ++end;
    return end;
}

int text_lines(const char* text) {
    int count = 0;
    char buf[256];
    while (*text) { text = next_line(text, buf, sizeof(buf)); ++count; }
    return count;
}

struct BodyContext {
    const char* text;
    const core::GameData* data;
    const core::Mail* mail;
};

bool weekly_game(const core::GameRec& game, const core::Mail& mail) {
    return game.release <= mail.date && game.release > mail.date - 7;
}

int body_lines(const BodyContext& body) {
    int count = text_lines(body.text);
    if (body.mail->kind == core::MailKind::kNewGames)
        for (int i = 0; i < body.data->game_count; ++i)
            if (weekly_game(body.data->games[i], *body.mail))
                count += text_lines(core::data_string(*body.data, body.data->games[i].title));
    return count;
}

const char* body_line(int row, char*, int, void* user) {
    const BodyContext& body = *static_cast<BodyContext*>(user);
    const char* text = body.text;
    int lines = text_lines(text);
    if (row >= lines && body.mail->kind == core::MailKind::kNewGames) {
        row -= lines;
        for (int i = 0; i < body.data->game_count; ++i) {
            if (!weekly_game(body.data->games[i], *body.mail)) continue;
            text = core::data_string(*body.data, body.data->games[i].title);
            lines = text_lines(text);
            if (row < lines) break;
            row -= lines;
        }
    }
    static char line[256];
    line[0] = '\0';
    for (int i = 0; i <= row && *text; ++i) text = next_line(text, line, sizeof(line));
    return line;
}

const char* subject(const State& s, const core::Mail& m) {
    if (m.kind == core::MailKind::kEvent && m.ref < s.data->event_count)
        return core::data_string(*s.data, s.data->events[m.ref].present_text);
    const int kind = static_cast<int>(m.kind);
    return kind >= 0 && kind < 5 ? s.kinds[kind] : s.unavailable;
}

const char* inbox_text(int row, char* buf, int cap, void* user) {
    const Context& ctx = *static_cast<Context*>(user);
    const core::Mail& m = ctx.world->mailbox.mail[row];
    const core::Ymd date = core::ymd_from_date(m.date);
    std::snprintf(buf, static_cast<size_t>(cap), g_state.row_fmt, m.read ? g_state.read : g_state.unread,
                  date.day, date.month, subject(g_state, m));
    return buf;
}

int active_event(const core::World& w, int mail) {
    for (int i = 0; i < core::kMaxActiveEvents; ++i)
        if (w.events.active[i].event != core::kNone && w.events.active[i].mail == mail &&
            w.events.active[i].event == w.mailbox.mail[mail].ref) return i;
    return -1;
}

struct ChoiceContext { State* state; core::World* world; int active; };

const char* choice_text(int i, char* buf, int cap, void* user) {
    const ChoiceContext& c = *static_cast<ChoiceContext*>(user);
    const core::EventRec& e = c.world->data->events[c.world->events.active[c.active].event];
    const char* text = core::data_string(*c.world->data, c.world->data->choices[e.choice_first + i].text);
    if (core::event_choice_available(*c.world, static_cast<uint8_t>(c.active), static_cast<uint8_t>(i)))
        return text;
    std::snprintf(buf, static_cast<size_t>(cap), c.state->locked_fmt, text);
    return buf;
}

const char* make_body(State& s, const core::Mail& m) {
    if (m.kind == core::MailKind::kEvent) return subject(s, m);
    char money[32];
    format_money(m.amount, money, sizeof(money));
    if (m.kind == core::MailKind::kReport || m.kind == core::MailKind::kBill) {
        std::snprintf(s.body_text, sizeof(s.body_text), m.kind == core::MailKind::kBill ? s.bill_fmt : s.report_fmt,
                      money);
        return s.body_text;
    }
    if (m.kind == core::MailKind::kDelivery) return s.delivery;
    if (m.kind != core::MailKind::kNewGames) return s.unavailable;
    for (int i = 0; i < s.data->game_count; ++i)
        if (weekly_game(s.data->games[i], m)) return s.news;
    return s.no_news;
}

void on_open(Context&, void* user) {
    State& s = *static_cast<State*>(user);
    s = State{};
    const char* kinds[] = {"os.mail.event", "os.mail.report", "os.mail.news", "os.mail.bill", "os.mail.delivery"};
    for (int i = 0; i < 5; ++i) s.kinds[i] = str(kinds[i]);
    s.empty = str("os.mail.empty");
    s.unavailable = str("os.mail.unavailable");
    s.open = str("os.mail.open");
    s.back = str("os.mail.back");
    s.reply = str("os.mail.reply");
    s.answered = str("os.mail.answered");
    s.expired = str("os.mail.expired");
    s.failed = str("os.mail.failed");
    s.row_fmt = str("os.mail.row");
    s.read = str("os.mail.read");
    s.unread = str("os.mail.unread");
    s.locked_fmt = str("os.mail.locked");
    s.report_fmt = str("os.mail.report_body");
    s.bill_fmt = str("os.mail.bill_body");
    s.delivery = str("os.mail.delivery_body");
    s.news = str("os.mail.news_body");
    s.no_news = str("os.mail.no_news");
    s.date_fmt = str("os.mail.date");
}

bool same_mail(const core::Mail& a, const core::Mail& b) {
    return a.kind == b.kind && a.ref == b.ref && a.date == b.date && a.amount == b.amount;
}

void on_frame(Context& ctx, void* user) {
    State& s = *static_cast<State*>(user);
    if (!ctx.world || !ctx.world->data) { label(ctx, {4, 4, 468, 28}, s.unavailable); return; }
    if (s.owner != ctx.world || s.data != ctx.world->data) { s.opened = -1; s.inbox = ListState{}; }
    s.owner = ctx.world;
    s.data = ctx.world->data;
    const int count = ctx.world->mailbox.count;
    // Ящик может вытеснить прочитанное письмо и сдвинуть индексы, пока окно открыто.
    if (s.opened >= count || (s.opened >= 0 && !same_mail(s.opened_mail, ctx.world->mailbox.mail[s.opened])))
        s.opened = -1;
    if (s.opened >= 0 && consume(ctx, platform::kButtonCircle)) { s.opened = -1; set_focus(ctx, kInbox); }
    if (s.opened < 0) {
        const ListEvent event = list(ctx, kInbox, {4, 4, 468, 188}, s.inbox, count, inbox_text, &ctx);
        if (count == 0) label(ctx, {8, 26, 456, 28}, s.empty, kColTextDim);
        const bool open = button(ctx, kOpen, {316, 200, 156, 16}, s.open,
                                 s.inbox.selected < 0 ? kWidgetDisabled : 0);
        if ((event == ListEvent::kActivated || open) && s.inbox.selected >= 0 && s.inbox.selected < count) {
            s.opened = s.inbox.selected;
            core::cmd_mail_read(*ctx.world, static_cast<uint8_t>(s.opened));
            s.opened_mail = ctx.world->mailbox.mail[s.opened];
            s.answer = 0;
            s.body = ListState{};
            s.status = nullptr;
            set_focus(ctx, kBody);
        }
        return;
    }
    const core::Mail m = ctx.world->mailbox.mail[s.opened];
    char buf[128];
    const core::Ymd date = core::ymd_from_date(m.date);
    std::snprintf(buf, sizeof(buf), s.date_fmt, date.day, date.month, date.year, subject(s, m));
    label(ctx, {4, 2, 468, 16}, buf, kColAccent);
    BodyContext body{make_body(s, m), s.data, &m};
    list(ctx, kBody, {4, 22, 468, 126}, s.body, body_lines(body), body_line, &body);
    if (m.kind == core::MailKind::kEvent) {
        const int active = active_event(*ctx.world, s.opened);
        if (!m.answered && active >= 0) {
            const core::EventRec& e = s.data->events[m.ref];
            ChoiceContext c{&s, ctx.world, active};
            choice(ctx, kChoice, {4, 154, 468, 16}, &s.answer, e.choice_count, choice_text, &c);
            const bool enabled = core::event_choice_available(*ctx.world, static_cast<uint8_t>(active),
                                                               static_cast<uint8_t>(s.answer));
            if (button(ctx, kAnswer, {4, 178, 292, 16}, s.reply, enabled ? 0 : kWidgetDisabled)) {
                const core::CmdResult result = core::cmd_event_choose(*ctx.world, static_cast<uint8_t>(active),
                                                                       static_cast<uint8_t>(s.answer));
                s.status = result == core::CmdResult::kOk ? s.answered : s.failed;
            }
        } else {
            label(ctx, {4, 154, 468, 16}, m.answered ? s.answered : s.expired, kColTextDim);
            if (m.answered && m.ref < s.data->event_count && m.choice < s.data->events[m.ref].choice_count)
                label(ctx, {4, 174, 292, 32}, core::data_string(*s.data,
                      s.data->choices[s.data->events[m.ref].choice_first + m.choice].text));
        }
    }
    if (button(ctx, kBack, {316, 200, 156, 16}, s.back)) { s.opened = -1; set_focus(ctx, kInbox); }
    if (s.status) label(ctx, {4, 218, 468, 16}, s.status, kColTextDim);
}
}  // namespace

AppId mail_register() {
    AppDesc d{};
    d.id = "mail";
    d.title_key = "os.mail.title";
    d.icon_text = "os.mail.icon";
    d.icon_color = kColWarn;
    d.flags = kAppOnDesktop | kAppMaximized;
    d.on_open = on_open;
    d.on_frame = on_frame;
    d.user = &g_state;
    return register_app(d);
}
}  // namespace ui
