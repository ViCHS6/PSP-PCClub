#pragma once
// КОНТРАКТ (T020). Формулы экономики GDD §7 — чистые функции без состояния и без случайности.
// Реализация — src/core/formulas.cpp (T022). Тесты — tests/core/ (T024, пишутся по GDD,
// не по реализации). Симуляция (core_api.h) считает всё только через эти функции,
// поэтому тесты формул проверяют и поведение игры.
//
// Соглашения:
//   - имена параметров — как в GDD; вероятности возвращаются как есть, бросок делает ядро
//     через rng_chance();
//   - вероятность всегда обрезана в [0, 1], удовлетворённость — в [0, 100] только там,
//     где это сказано (дельты не обрезаются);
//   - числа, которые в GDD названы стартовыми/по умолчанию, приходят параметрами (из данных);
//     коэффициенты самих формул — константы внутри реализации;
//   - деньги — int32_t, рубли; перевод float → рубли только через money_round().
//
// Не входят (появятся с вехой M8 отдельным изменением контракта): §7.8 вирусы, §7.10 конкурент,
// §7.11 мини-игры посетителей, §7.12 турниры. Без конкурента F_comp = 1.
//
//     using namespace core::formula;
//     const float lambda = arrival_rate(1.6f, 1.0f, 1.4f, 1.0f, rep_factor(30.0f),
//                                       1.0f, library_factor(0.8f), 1.0f, 1.0f);
//     if (core::rng_chance(world.rng, arrival_prob_per_minute(lambda))) { /* новый посетитель */ }
#include <cstdint>

namespace core {
namespace formula {

// Округление рублей: к ближайшему целому, половина — от нуля (2,5 → 3, −2,5 → −3).
int32_t money_round(float rubles);

// ---------------------------------------------------------------------------
// §7.1. Поток посетителей

// F_rep = 0,5 + rep / 100. rep обрезается в [0, 100] → результат 0,5…1,5.
float rep_factor(float rep);

// F_lib_a = clamp(0,6 + 0,8 · L_a, 0,6, 1,4).
float library_factor(float library_appeal);

// λ_a(h, d) = B_a · H_a(h) · W_a(d) · S_a(d) · F_rep · F_ad_a · F_lib_a · F_comp · F_evt_a,
// посетителей в игровой час. Отрицательные множители недопустимы (результат ≥ 0 не гарантируется).
float arrival_rate(float base, float hourly, float weekday, float season, float f_rep,
                   float f_ad, float f_lib, float f_comp, float f_evt);

// p = λ / 60 — вероятность появления за тик (минуту), обрезана в [0, 1].
float arrival_prob_per_minute(float lambda);

// ---------------------------------------------------------------------------
// §7.2. Генерация посетителя

// T_plan = min(hours_roll, floor(money / price_hour)) — целые часы. price_hour ≤ 0 → hours_roll.
// 0 означает, что посетителю не хватает даже на час: он уходит, не дойдя до стойки.
int plan_hours(int hours_roll, int32_t money, int32_t price_hour);

// ---------------------------------------------------------------------------
// §7.3. Решение у стойки

// Стартовая удовлетворённость, если посетитель согласился на альтернативную игру: −15.
float alt_game_sat_penalty();

// Изменение репутации, если посетитель ушёл, не найдя нужной игры: −0,2.
float no_game_rep_penalty();

// P_exp = P_exp_a без конкурента; 0,6 · P_exp_a + 0,4 · price_comp с конкурентом.
float expected_price(float price_expect_a, bool competitor_active, float price_competitor);

// p_accept = clamp(1 − e_a · (price_hour / P_exp − 1), 0, 1). При цене ≤ P_exp — 1.
// Пример GDD: e = 1,2, цена 20, P_exp 15 → 0,6.
float price_accept_prob(float elasticity, float price_hour, float price_expected);

// Прибавка к стартовой sat за цену ниже ожидаемой: min(10 · (1 − price_hour / P_exp), 10),
// при цене ≥ P_exp — 0.
float price_sat_bonus(float price_hour, float price_expected);

// Изменение sat за минуту ожидания в очереди: −1.
float queue_sat_per_minute();

// Оплата вперёд: price_hour · T_plan.
int32_t session_payment(int32_t price_hour, int hours);

// ---------------------------------------------------------------------------
// §7.4. Производительность ПК для игры

// score_eff_k = score_k · (1 − 0,4 · overheat) · (virus ? 0,8 : 1). overheat в [0, 1].
float effective_score(float score, float overheat, bool virus);

// perf = min(r_cpu, r_gpu, r_ram), r_k = score_eff_k / req_k. Требование ≤ 0 не ограничивает
// (r_k = +∞ для этого k); все три ≤ 0 → perf = 1,5 (без ограничений по железу).
float pc_perf(float cpu_eff, float gpu_eff, float ram_eff, float req_cpu, float req_gpu,
              float req_ram);

// Игра запускается при perf ≥ 0,7; иначе посетитель не сядет за этот ПК.
bool perf_runs(float perf);

// perf_term в Δsat за час: perf < 0,7 → 0 (игра не идёт, отдельный случай);
// 0,7 ≤ perf < 1,0 → −40 · (1 − perf); 1,0 ≤ perf < 1,5 → 0;
// perf ≥ 1,5 → +5, если likes_graphics, иначе 0.
float perf_sat_per_hour(float perf, bool likes_graphics);

// ---------------------------------------------------------------------------
// §7.5. Удовлетворённость

// Стартовая sat: clamp(70 + modifiers, 0, 100). modifiers — сумма стартовых
// модификаторов (alt_game_sat_penalty, price_sat_bonus, потери в очереди).
float sat_start(float modifiers);

// comfort(c) = clamp(Σ comfort компонентов, −5, 10).
float comfort_clamp(float comfort_sum);

// environment = clamp(env, −10, 5).
float environment_clamp(float environment);

// Δsat за час = perf_term + comfort + environment − 10 · unresolved + 5 · resolved + event_mods.
// comfort и environment передаются уже обрезанными. Результат не обрезается.
float sat_delta_hour(float perf_term, float comfort, float environment, int unresolved_requests,
                     int resolved_requests, float event_mods);

// sat < 20 — посетитель уходит раньше, без возврата денег.
bool leaves_early(float sat);

// p_ext = (sat / 100)² · ext_a · [money_left ≥ price_hour]. sat обрезается в [0, 100].
float extend_prob(float sat, float ext_a, int32_t money_left, int32_t price_hour);

// Δrep при уходе = w_a · (sat − 55) / 45.
float leave_rep_delta(float rep_weight, float sat);

// relation += (sat − 55) / 10 — для постоянного клиента.
float relation_delta(float sat);

// Условие для броска «стать постоянным»: visits ≥ 3 и avg_sat > 60.
bool regular_eligible(int visits, float avg_sat);

// Вероятность стать постоянным за визит при выполненном условии: 0,3.
float regular_convert_prob();

// ---------------------------------------------------------------------------
// §7.6. Популярность игр и библиотека

// Популярность без модификаторов для игры возрастом t дней:
// t < 0 → 0; t < R → peak · (0,5 + 0,5 · t / R); иначе peak · max(floor, 0,5^((t − R) / H)).
// R = 0 → сразу ветка спада; H = 0 → floor (после R).
// Пример GDD: Контр-Удар peak 1, R 30, H 400, floor 0,6.
float game_popularity(int age_days, float peak, int rise_days, int half_life_days, float floor);

// L_a = Σ top-5 installed / Σ top-5 released. Массивы — value_a(g) = pop(g) · aff(a, genre)
// для установленных (installed) и всех вышедших (released) игр, в любом порядке, не меняются.
// Берутся до 5 наибольших. Σ released = 0 → 0. Результат обрезан в [0, 1].
float library_appeal(const float* installed_values, int installed_count,
                     const float* released_values, int released_count);

// ---------------------------------------------------------------------------
// §7.7. Износ, пыль, поломки (всё — за час нагрузки ПК)

// Прирост пыли: 1,0 · env_dust (курение ×1,5, уборщица ×0,6, модификаторы — в env_dust).
float dust_per_hour(float env_dust);

// overheat = clamp((dust − 40) / 60, 0, 1) · (aircon ? aircon_mult : 1). aircon_mult — из данных (0,5).
float overheat(float dust, bool aircon, float aircon_mult);

// wear_k += r_k · (1 + overheat).
float wear_per_hour(float wear_rate, float overheat);

// p_fail_k = f0_k · (wear_k / 100)³ + (cpu_or_gpu ? 0,02 · overheat : 0). wear ≥ 100 → 1.
float fail_prob(float fail_base, float wear, bool cpu_or_gpu, float overheat);

// p_hang = 0,02 + 0,05 · overheat + (virus ? 0,10 : 0).
float hang_prob(float overheat, bool virus);

// Ремонт мини-игрой: wear − (30 + 30 · quality), не ниже 0. quality обрезается в [0, 1].
float wear_after_repair(float wear, float quality);

// Компонент сломан навсегда при wear ≥ 100 — только замена.
bool wear_broken(float wear);

// ---------------------------------------------------------------------------
// §7.9. Цены на железо (нужны Магазину в вертикальном срезе)

// price = base · 0,97^months · f_fx · f_supply, округление money_round. months < 0 → 0.
int32_t item_price(int32_t base_price, int months_since_release, float f_fx, float f_supply);

// F_supply = 1,15 в первый месяц после выхода (months_since_release == 0), иначе 1,0.
float supply_factor(int months_since_release);

// Продажа б/у: money_round(0,5 · price · (1 − wear / 100)), wear обрезается в [0, 100].
int32_t used_price(int32_t price, float wear);

// ---------------------------------------------------------------------------
// §7.13. Буфет

// p_buy за час = hunger_a · clamp(1,6 − markup, 0, 1,2), markup = sell / buy.
float buy_prob(float hunger, float markup);

// Изменение sat, если просьба «а есть...?» не решена: −5.
float goods_missing_sat_penalty();

// ---------------------------------------------------------------------------
// §7.14. Расходы

// Электричество за день: money_round((Σ часов нагрузки ПК · 0,25 + часы работы · 0,5) · price_kwh).
int32_t electricity_cost(float pc_load_hours, float open_hours, float price_kwh);

// Налог: money_round(month_revenue · tax_rate). Отрицательная выручка → 0.
int32_t tax(int32_t month_revenue, float tax_rate);

// Проценты по займу за неделю: money_round(debt · loan_rate_week). debt ≤ 0 → 0.
int32_t loan_interest(int32_t debt, float loan_rate_week);

// ---------------------------------------------------------------------------
// §7.15. Репутация

// Ежедневный дрейф: rep + (40 − rep) · 0,005, обрезано в [0, 100].
float rep_after_daily_drift(float rep);

// clamp(rep, 0, 100).
float rep_clamp(float rep);

}  // namespace formula
}  // namespace core
