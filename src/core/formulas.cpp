// Формулы экономики GDD §7 (контракт — formulas_api.h).
#include "core/formulas_api.h"

#include <cmath>

namespace core {
namespace formula {

namespace {

float clampf(float x, float lo, float hi) { return x < lo ? lo : (x > hi ? hi : x); }

}  // namespace

int32_t money_round(float rubles) {
    return static_cast<int32_t>(rubles >= 0.0f ? rubles + 0.5f : rubles - 0.5f);
}

// ---------------------------------------------------------------------------
// §7.1

float rep_factor(float rep) { return 0.5f + clampf(rep, 0.0f, 100.0f) / 100.0f; }  // GDD §7.1

float library_factor(float library_appeal) {
    return clampf(0.6f + 0.8f * library_appeal, 0.6f, 1.4f);  // GDD §7.1
}

float arrival_rate(float base, float hourly, float weekday, float season, float f_rep, float f_ad,
                   float f_lib, float f_comp, float f_evt) {
    return base * hourly * weekday * season * f_rep * f_ad * f_lib * f_comp * f_evt;  // GDD §7.1
}

float arrival_prob_per_minute(float lambda) { return clampf(lambda / 60.0f, 0.0f, 1.0f); }

// ---------------------------------------------------------------------------
// §7.2

int plan_hours(int hours_roll, int32_t money, int32_t price_hour) {
    if (price_hour <= 0) return hours_roll;
    const int32_t affordable = money > 0 ? money / price_hour : 0;  // GDD §7.2
    const int hours = hours_roll < affordable ? hours_roll : static_cast<int>(affordable);
    return hours > 0 ? hours : 0;
}

// ---------------------------------------------------------------------------
// §7.3

float alt_game_sat_penalty() { return -15.0f; }
float no_game_rep_penalty() { return -0.2f; }

float expected_price(float price_expect_a, bool competitor_active, float price_competitor) {
    if (!competitor_active) return price_expect_a;
    return 0.6f * price_expect_a + 0.4f * price_competitor;  // GDD §7.3
}

float price_accept_prob(float elasticity, float price_hour, float price_expected) {
    if (price_expected <= 0.0f) return price_hour <= 0.0f ? 1.0f : 0.0f;
    if (price_hour <= price_expected) return 1.0f;
    return clampf(1.0f - elasticity * (price_hour / price_expected - 1.0f), 0.0f, 1.0f);  // GDD §7.3
}

float price_sat_bonus(float price_hour, float price_expected) {
    if (price_expected <= 0.0f || price_hour >= price_expected) return 0.0f;
    const float bonus = 10.0f * (1.0f - price_hour / price_expected);  // GDD §7.3
    return bonus < 10.0f ? bonus : 10.0f;
}

float queue_sat_per_minute() { return -1.0f; }

int32_t session_payment(int32_t price_hour, int hours) { return price_hour * hours; }

// ---------------------------------------------------------------------------
// §7.4

float effective_score(float score, float overheat, bool virus) {
    const float oh = clampf(overheat, 0.0f, 1.0f);
    return score * (1.0f - 0.4f * oh) * (virus ? 0.8f : 1.0f);  // GDD §7.4
}

float pc_perf(float cpu_eff, float gpu_eff, float ram_eff, float req_cpu, float req_gpu,
              float req_ram) {
    bool limited = false;
    float perf = 0.0f;
    const float eff[3] = {cpu_eff, gpu_eff, ram_eff};
    const float req[3] = {req_cpu, req_gpu, req_ram};
    for (int k = 0; k < 3; ++k) {
        if (req[k] <= 0.0f) continue;
        const float r = eff[k] / req[k];  // GDD §7.4
        if (!limited || r < perf) perf = r;
        limited = true;
    }
    return limited ? perf : 1.5f;
}

bool perf_runs(float perf) { return perf >= 0.7f; }

float perf_sat_per_hour(float perf, bool likes_graphics) {
    if (perf < 0.7f) return 0.0f;
    if (perf < 1.0f) return -40.0f * (1.0f - perf);  // GDD §7.4
    if (perf < 1.5f) return 0.0f;
    return likes_graphics ? 5.0f : 0.0f;
}

// ---------------------------------------------------------------------------
// §7.5

float sat_start(float modifiers) { return clampf(70.0f + modifiers, 0.0f, 100.0f); }
float comfort_clamp(float comfort_sum) { return clampf(comfort_sum, -5.0f, 10.0f); }
float environment_clamp(float environment) { return clampf(environment, -10.0f, 5.0f); }

float sat_delta_hour(float perf_term, float comfort, float environment, int unresolved_requests,
                     int resolved_requests, float event_mods) {
    // GDD §7.5
    return perf_term + comfort + environment - 10.0f * static_cast<float>(unresolved_requests) +
           5.0f * static_cast<float>(resolved_requests) + event_mods;
}

bool leaves_early(float sat) { return sat < 20.0f; }

float extend_prob(float sat, float ext_a, int32_t money_left, int32_t price_hour) {
    if (money_left < price_hour) return 0.0f;
    const float s = clampf(sat, 0.0f, 100.0f) / 100.0f;
    return clampf(s * s * ext_a, 0.0f, 1.0f);  // GDD §7.5
}

float leave_rep_delta(float rep_weight, float sat) { return rep_weight * (sat - 55.0f) / 45.0f; }
float relation_delta(float sat) { return (sat - 55.0f) / 10.0f; }

bool regular_eligible(int visits, float avg_sat) { return visits >= 3 && avg_sat > 60.0f; }
float regular_convert_prob() { return 0.3f; }

// ---------------------------------------------------------------------------
// §7.6

float game_popularity(int age_days, float peak, int rise_days, int half_life_days, float floor) {
    if (age_days < 0) return 0.0f;
    if (age_days < rise_days) {
        return peak * (0.5f + 0.5f * static_cast<float>(age_days) / static_cast<float>(rise_days));
    }
    if (half_life_days <= 0) return peak * floor;
    const float decay = std::exp2(-static_cast<float>(age_days - rise_days) /
                                  static_cast<float>(half_life_days));  // 0,5^((t − R) / H)
    return peak * (decay > floor ? decay : floor);
}

namespace {

// Сумма до пяти наибольших значений без изменения массива.
float top5_sum(const float* values, int count) {
    float top[5] = {0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
    int n = 0;
    for (int i = 0; i < count; ++i) {
        float v = values[i];
        if (n < 5) {
            top[n++] = v;
        } else {
            int min_i = 0;
            for (int j = 1; j < 5; ++j) {
                if (top[j] < top[min_i]) min_i = j;
            }
            if (v > top[min_i]) top[min_i] = v;
        }
    }
    float s = 0.0f;
    for (int j = 0; j < n; ++j) s += top[j];
    return s;
}

}  // namespace

float library_appeal(const float* installed_values, int installed_count,
                     const float* released_values, int released_count) {
    const float released = top5_sum(released_values, released_count);
    if (released <= 0.0f) return 0.0f;
    return clampf(top5_sum(installed_values, installed_count) / released, 0.0f, 1.0f);  // GDD §7.6
}

// ---------------------------------------------------------------------------
// §7.7

float dust_per_hour(float env_dust) { return 1.0f * env_dust; }

float overheat(float dust, bool aircon, float aircon_mult) {
    return clampf((dust - 40.0f) / 60.0f, 0.0f, 1.0f) * (aircon ? aircon_mult : 1.0f);  // GDD §7.7
}

float wear_per_hour(float wear_rate, float overheat) { return wear_rate * (1.0f + overheat); }

float fail_prob(float fail_base, float wear, bool cpu_or_gpu, float overheat) {
    if (wear >= 100.0f) return 1.0f;
    const float w = clampf(wear, 0.0f, 100.0f) / 100.0f;
    const float p = fail_base * w * w * w + (cpu_or_gpu ? 0.02f * overheat : 0.0f);  // GDD §7.7
    return clampf(p, 0.0f, 1.0f);
}

float hang_prob(float overheat, bool virus) {
    return clampf(0.02f + 0.05f * overheat + (virus ? 0.10f : 0.0f), 0.0f, 1.0f);  // GDD §7.7
}

float wear_after_repair(float wear, float quality) {
    const float w = wear - (30.0f + 30.0f * clampf(quality, 0.0f, 1.0f));  // GDD §7.7
    return w > 0.0f ? w : 0.0f;
}

bool wear_broken(float wear) { return wear >= 100.0f; }

// ---------------------------------------------------------------------------
// §7.9

int32_t item_price(int32_t base_price, int months_since_release, float f_fx, float f_supply) {
    const int months = months_since_release > 0 ? months_since_release : 0;
    const float decay = std::pow(0.97f, static_cast<float>(months));  // GDD §7.9
    return money_round(static_cast<float>(base_price) * decay * f_fx * f_supply);
}

float supply_factor(int months_since_release) { return months_since_release == 0 ? 1.15f : 1.0f; }

int32_t used_price(int32_t price, float wear) {
    const float w = clampf(wear, 0.0f, 100.0f);
    return money_round(0.5f * static_cast<float>(price) * (1.0f - w / 100.0f));  // GDD §7.9
}

// ---------------------------------------------------------------------------
// §7.13

float buy_prob(float hunger, float markup) {
    return clampf(hunger * clampf(1.6f - markup, 0.0f, 1.2f), 0.0f, 1.0f);  // GDD §7.13
}

float goods_missing_sat_penalty() { return -5.0f; }

// ---------------------------------------------------------------------------
// §7.14

int32_t electricity_cost(float pc_load_hours, float open_hours, float price_kwh) {
    return money_round((pc_load_hours * 0.25f + open_hours * 0.5f) * price_kwh);  // GDD §7.14
}

int32_t tax(int32_t month_revenue, float tax_rate) {
    if (month_revenue <= 0) return 0;
    return money_round(static_cast<float>(month_revenue) * tax_rate);
}

int32_t loan_interest(int32_t debt, float loan_rate_week) {
    if (debt <= 0) return 0;
    return money_round(static_cast<float>(debt) * loan_rate_week);
}

// ---------------------------------------------------------------------------
// §7.15

float rep_after_daily_drift(float rep) {
    return clampf(rep + (40.0f - rep) * 0.005f, 0.0f, 100.0f);  // GDD §7.15
}

float rep_clamp(float rep) { return clampf(rep, 0.0f, 100.0f); }

}  // namespace formula
}  // namespace core
