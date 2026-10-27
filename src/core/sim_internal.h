#pragma once
// Внутренние функции ядра, общие для файлов src/core (T022) и системы событий (T023).
// НЕ контракт: снаружи ядра не вызывать.
#include <cstdint>

#include "core/core_api.h"

namespace core {
namespace detail {

// --- Учёт (world.cpp)
void push_notice(World& w, NoticeKind kind, uint8_t a = 0, uint8_t visitor = kNoVisitor,
                 uint8_t pc = kNoPc, Index ref = kNone, int32_t value = 0);
void add_revenue(World& w, Revenue kind, int32_t amount);
void add_expense(World& w, Expense kind, int32_t amount);
// Новое письмо; возвращает его индекс.
int add_mail(World& w, MailKind kind, Index ref, int32_t amount);
// Деньги < 0 → разница в долг (§5.4).
void cover_debt(World& w);
// Сбросить кэши популярности и L_a (установка игр, модификаторы pop).
void invalidate_caches(World& w);
void rebuild_caches(World& w);

// --- Клуб (world.cpp)
bool pc_complete(const World& w, uint8_t pc);           // все обязательные компоненты на месте
bool game_net_ok(const World& w, Index game);           // хватает интернета (§6.3)
bool game_playable(const World& w, uint8_t pc, Index game);  // установлена, perf ≥ 0,7, сеть
float value_for(const World& w, Index archetype, Index game);  // value_a(g) по кэшу (§7.6)
const UpgradeRec* owned_upgrade_of_kind(const World& w, uint8_t kind);
// Купленный апгрейд: бит, замена младшего уровня того же kind, места/интернет/сеть.
void own_upgrade(World& w, Index upgrade);
// Требование requires выполнено: куплен он сам или старший уровень того же kind.
bool upgrade_requirement_met(const World& w, Index upgrade);
int months_between(DayNum from, DayNum to);  // полных месяцев (§7.9)
float ad_factor(const World& w, Index archetype);       // F_ad_a (§7.1)
float modifier_product(const World& w, uint8_t target, Index arg);
void free_pc(World& w, uint8_t pc);                     // посетитель встал из-за ПК

// --- Посетители (visitors.cpp)
// Создать посетителя у входа (эффект spawn_visitor тоже идёт сюда). regular = kNone —
// анонимный. Нет слота или очередь полна → kNoVisitor (учтено как kQueueFull).
uint8_t spawn_visitor(World& w, Index archetype, Index regular);
void start_leave(World& w, uint8_t v, LeaveReason reason);
void visitors_tick(World& w);
void visitors_close(World& w, bool night);  // 24:00
void visitors_morning(World& w);            // 8:00 после ночи
void set_request(World& w, uint8_t v, Request r, Index arg);
void resolve_request(World& w, uint8_t v);  // просьба решена: +5 в этом часе

// --- ПК (pcs.cpp)
void pcs_tick(World& w);
// ПК после ремонта/замены/перезагрузки: kBusy с посетителем, иначе kIdle.
void pc_restore(World& w, uint8_t pc);

// --- Условия (conditions.cpp) — нужны и разблокировке постоянных клиентов, и T023.
float cond_var_value(const World& w, uint8_t var, Index arg);
bool cond_holds(const World& w, const CondRec& c);
bool conds_hold(const World& w, uint16_t first, uint8_t count);

// --- Хуки системы событий (заглушки в hooks_stub.cpp до T023)
void events_on_action(World& w, ActionId action, Index ref);
void events_tick(World& w);  // каждый тик, шаг 4

}  // namespace detail
}  // namespace core
