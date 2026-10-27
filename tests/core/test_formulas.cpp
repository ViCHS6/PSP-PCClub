#include <doctest/doctest.h>

#include <array>
#include <cmath>
#include <initializer_list>

#include "core/formulas_api.h"

using namespace core::formula;

TEST_CASE("§7 money_round: половина округляется от нуля") {
    CHECK(money_round(2.5f) == 3);
    CHECK(money_round(-2.5f) == -3);
    CHECK(money_round(2.4f) == 2);
    CHECK(money_round(2.6f) == 3);
    CHECK(money_round(0.0f) == 0);
    CHECK(money_round(-0.4f) == 0);
    CHECK(money_round(1199.5f) == 1200);
}

TEST_CASE("§7.1 rep_factor: границы и монотонность репутации") {
    CHECK(rep_factor(0.0f) == doctest::Approx(0.5f).epsilon(1e-4f));
    CHECK(rep_factor(30.0f) == doctest::Approx(0.8f).epsilon(1e-4f));
    CHECK(rep_factor(50.0f) == doctest::Approx(1.0f).epsilon(1e-4f));
    CHECK(rep_factor(100.0f) == doctest::Approx(1.5f).epsilon(1e-4f));
    CHECK(rep_factor(-20.0f) == doctest::Approx(0.5f).epsilon(1e-4f));
    CHECK(rep_factor(150.0f) == doctest::Approx(1.5f).epsilon(1e-4f));

    for (int rep = -50; rep < 150; ++rep) {
        CAPTURE(rep);
        CHECK(rep_factor(static_cast<float>(rep + 1)) >= rep_factor(static_cast<float>(rep)));
    }
}

TEST_CASE("§7.1 library_factor: привлекательность и обрезка") {
    CHECK(library_factor(0.0f) == doctest::Approx(0.6f).epsilon(1e-4f));
    CHECK(library_factor(0.5f) == doctest::Approx(1.0f).epsilon(1e-4f));
    CHECK(library_factor(0.8f) == doctest::Approx(1.24f).epsilon(1e-4f));
    CHECK(library_factor(1.0f) == doctest::Approx(1.4f).epsilon(1e-4f));
    CHECK(library_factor(-1.0f) == doctest::Approx(0.6f).epsilon(1e-4f));
    CHECK(library_factor(2.0f) == doctest::Approx(1.4f).epsilon(1e-4f));
}

TEST_CASE("§7.1 arrival_rate: все девять множителей влияют на поток") {
    const std::array<float, 9> example = {{1.6f, 1.0f, 1.4f, 1.0f, 0.8f,
                                         1.0f, 1.24f, 1.0f, 1.0f}};
    const auto rate = [](const std::array<float, 9>& factors) {
        return arrival_rate(factors[0], factors[1], factors[2], factors[3], factors[4],
                            factors[5], factors[6], factors[7], factors[8]);
    };
    CHECK(rate(example) == doctest::Approx(2.22208f).epsilon(1e-4f));
    CHECK(arrival_rate(1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f)
          == doctest::Approx(1.0f).epsilon(1e-4f));

    for (std::size_t position = 0; position < example.size(); ++position) {
        CAPTURE(position);
        auto factors = example;
        factors[position] = 0.0f;
        CHECK(rate(factors) == doctest::Approx(0.0f).epsilon(1e-4f));
        factors = example;
        factors[position] *= 2.0f;
        CHECK(rate(factors) == doctest::Approx(4.44416f).epsilon(1e-4f));
        CHECK(rate(factors) == doctest::Approx(2.0f * rate(example)).epsilon(1e-4f));
    }
}

TEST_CASE("§7.1 arrival_prob_per_minute: перевод потока в вероятность") {
    CHECK(arrival_prob_per_minute(6.0f) == doctest::Approx(0.1f).epsilon(1e-4f));
    CHECK(arrival_prob_per_minute(0.0f) == doctest::Approx(0.0f).epsilon(1e-6f));
    CHECK(arrival_prob_per_minute(2.22208f) == doctest::Approx(0.0370347f).epsilon(1e-4f));
    CHECK(arrival_prob_per_minute(60.0f) == doctest::Approx(1.0f).epsilon(1e-4f));
    CHECK(arrival_prob_per_minute(120.0f) == doctest::Approx(1.0f).epsilon(1e-4f));
    CHECK(arrival_prob_per_minute(-5.0f) == doctest::Approx(0.0f).epsilon(1e-6f));
}

TEST_CASE("§7.2 plan_hours: целые часы по доступным деньгам") {
    CHECK(plan_hours(3, 100, 20) == 3);
    CHECK(plan_hours(5, 50, 20) == 2);
    CHECK(plan_hours(2, 40, 20) == 2);
    CHECK(plan_hours(3, 19, 20) == 0);
    CHECK(plan_hours(0, 100, 20) == 0);
    CHECK(plan_hours(4, 1000, 0) == 4);
    CHECK(plan_hours(4, 1000, -5) == 4);
}

TEST_CASE("§7.3 alt_game_sat_penalty: штраф за альтернативу") {
    CHECK(alt_game_sat_penalty() == doctest::Approx(-15.0f).epsilon(1e-4f));
}

TEST_CASE("§7.3 no_game_rep_penalty: потеря репутации без нужной игры") {
    CHECK(no_game_rep_penalty() == doctest::Approx(-0.2f).epsilon(1e-4f));
}

TEST_CASE("§7.3 expected_price: влияние конкурента") {
    CHECK(expected_price(15.0f, false, 30.0f) == doctest::Approx(15.0f).epsilon(1e-4f));
    CHECK(expected_price(15.0f, true, 30.0f) == doctest::Approx(21.0f).epsilon(1e-4f));
    CHECK(expected_price(20.0f, true, 20.0f) == doctest::Approx(20.0f).epsilon(1e-4f));
}

TEST_CASE("§7.3 price_accept_prob: примеры, обрезка и монотонность") {
    CHECK(price_accept_prob(1.2f, 20.0f, 15.0f) == doctest::Approx(0.6f).epsilon(1e-4f));
    CHECK(price_accept_prob(1.0f, 20.0f, 20.0f) == doctest::Approx(1.0f).epsilon(1e-4f));
    CHECK(price_accept_prob(1.2f, 10.0f, 15.0f) == doctest::Approx(1.0f).epsilon(1e-4f));
    CHECK(price_accept_prob(0.6f, 45.0f, 30.0f) == doctest::Approx(0.7f).epsilon(1e-4f));
    CHECK(price_accept_prob(0.8f, 25.0f, 20.0f) == doctest::Approx(0.8f).epsilon(1e-4f));
    CHECK(price_accept_prob(1.2f, 40.0f, 15.0f) == doctest::Approx(0.0f).epsilon(1e-6f));

    for (int price = 5; price < 60; ++price) {
        CAPTURE(price);
        CHECK(price_accept_prob(1.2f, static_cast<float>(price + 1), 15.0f)
              <= price_accept_prob(1.2f, static_cast<float>(price), 15.0f));
    }
    for (int step = 0; step < 30; ++step) {
        CAPTURE(step);
        const float elasticity = static_cast<float>(step) / 10.0f;
        const float next_elasticity = static_cast<float>(step + 1) / 10.0f;
        CHECK(price_accept_prob(next_elasticity, 20.0f, 15.0f)
              <= price_accept_prob(elasticity, 20.0f, 15.0f));
    }
}

TEST_CASE("§7.3 price_sat_bonus: скидка даёт от нуля до десяти") {
    CHECK(price_sat_bonus(10.0f, 15.0f) == doctest::Approx(3.33333f).epsilon(1e-4f));
    CHECK(price_sat_bonus(7.5f, 15.0f) == doctest::Approx(5.0f).epsilon(1e-4f));
    CHECK(price_sat_bonus(0.0f, 15.0f) == doctest::Approx(10.0f).epsilon(1e-4f));
    CHECK(price_sat_bonus(15.0f, 15.0f) == doctest::Approx(0.0f).epsilon(1e-4f));
    CHECK(price_sat_bonus(20.0f, 15.0f) == doctest::Approx(0.0f).epsilon(1e-4f));

    for (int price = 0; price <= 40; ++price) {
        CAPTURE(price);
        const float bonus = price_sat_bonus(static_cast<float>(price), 15.0f);
        CHECK(bonus >= 0.0f);
        CHECK(bonus <= 10.0f);
    }
}

TEST_CASE("§7.3 queue_sat_per_minute: потеря за минуту очереди") {
    CHECK(queue_sat_per_minute() == doctest::Approx(-1.0f).epsilon(1e-4f));
}

TEST_CASE("§7.3 session_payment: предоплата за целые часы") {
    CHECK(session_payment(20, 3) == 60);
    CHECK(session_payment(20, 0) == 0);
    CHECK(session_payment(100, 1) == 100);
}

TEST_CASE("§7.4 effective_score: перегрев и вирус") {
    CHECK(effective_score(100.0f, 0.0f, false) == doctest::Approx(100.0f).epsilon(1e-4f));
    CHECK(effective_score(100.0f, 0.5f, false) == doctest::Approx(80.0f).epsilon(1e-4f));
    CHECK(effective_score(100.0f, 1.0f, false) == doctest::Approx(60.0f).epsilon(1e-4f));
    CHECK(effective_score(100.0f, 0.0f, true) == doctest::Approx(80.0f).epsilon(1e-4f));
    CHECK(effective_score(100.0f, 0.25f, true) == doctest::Approx(72.0f).epsilon(1e-4f));
    CHECK(effective_score(100.0f, 1.0f, true) == doctest::Approx(48.0f).epsilon(1e-4f));
}

TEST_CASE("§7.4 pc_perf: узкое место, отсутствующие требования и монотонность") {
    CHECK(pc_perf(100.0f, 200.0f, 128.0f, 100.0f, 100.0f, 64.0f)
          == doctest::Approx(1.0f).epsilon(1e-4f));
    CHECK(pc_perf(50.0f, 200.0f, 128.0f, 100.0f, 100.0f, 64.0f)
          == doctest::Approx(0.5f).epsilon(1e-4f));
    CHECK(pc_perf(80.0f, 90.0f, 64.0f, 100.0f, 100.0f, 64.0f)
          == doctest::Approx(0.8f).epsilon(1e-4f));
    CHECK(pc_perf(150.0f, 300.0f, 256.0f, 100.0f, 100.0f, 128.0f)
          == doctest::Approx(1.5f).epsilon(1e-4f));
    CHECK(pc_perf(100.0f, 100.0f, 64.0f, 0.0f, 100.0f, 64.0f)
          == doctest::Approx(1.0f).epsilon(1e-4f));
    CHECK(pc_perf(1.0f, 100.0f, 64.0f, -10.0f, 100.0f, 64.0f)
          == doctest::Approx(1.0f).epsilon(1e-4f));
    CHECK(pc_perf(10.0f, 10.0f, 10.0f, 0.0f, 0.0f, 0.0f)
          == doctest::Approx(1.5f).epsilon(1e-4f));

    for (int cpu = 0; cpu < 300; cpu += 10) {
        CAPTURE(cpu);
        CHECK(pc_perf(static_cast<float>(cpu + 10), 200.0f, 128.0f, 100.0f, 100.0f, 64.0f)
              >= pc_perf(static_cast<float>(cpu), 200.0f, 128.0f, 100.0f, 100.0f, 64.0f));
    }
}

TEST_CASE("§7.4 perf_runs: порог запуска игры") {
    CHECK(perf_runs(0.7f) == true);
    CHECK(perf_runs(0.69f) == false);
    CHECK(perf_runs(1.0f) == true);
    CHECK(perf_runs(0.0f) == false);
}

TEST_CASE("§7.4 perf_sat_per_hour: границы штрафа и бонуса графики") {
    CHECK(perf_sat_per_hour(0.5f, false) == doctest::Approx(0.0f).epsilon(1e-4f));
    CHECK(perf_sat_per_hour(0.69f, true) == doctest::Approx(0.0f).epsilon(1e-4f));
    CHECK(perf_sat_per_hour(0.7f, false) == doctest::Approx(-12.0f).epsilon(1e-4f));
    CHECK(perf_sat_per_hour(0.85f, false) == doctest::Approx(-6.0f).epsilon(1e-4f));
    CHECK(perf_sat_per_hour(0.9f, true) == doctest::Approx(-4.0f).epsilon(1e-4f));
    CHECK(perf_sat_per_hour(1.0f, false) == doctest::Approx(0.0f).epsilon(1e-4f));
    CHECK(perf_sat_per_hour(1.2f, true) == doctest::Approx(0.0f).epsilon(1e-4f));
    CHECK(perf_sat_per_hour(1.49f, true) == doctest::Approx(0.0f).epsilon(1e-4f));
    CHECK(perf_sat_per_hour(1.5f, true) == doctest::Approx(5.0f).epsilon(1e-4f));
    CHECK(perf_sat_per_hour(1.5f, false) == doctest::Approx(0.0f).epsilon(1e-4f));
    CHECK(perf_sat_per_hour(3.0f, true) == doctest::Approx(5.0f).epsilon(1e-4f));

    for (bool likes_graphics : {false, true}) {
        for (int step = 70; step < 300; ++step) {
            CAPTURE(likes_graphics);
            CAPTURE(step);
            const float perf = static_cast<float>(step) / 100.0f;
            const float next_perf = static_cast<float>(step + 1) / 100.0f;
            CHECK(perf_sat_per_hour(next_perf, likes_graphics)
                  >= perf_sat_per_hour(perf, likes_graphics));
        }
    }
}

TEST_CASE("§7.5 sat_start: стартовые модификаторы и границы") {
    CHECK(sat_start(0.0f) == doctest::Approx(70.0f).epsilon(1e-4f));
    CHECK(sat_start(-15.0f) == doctest::Approx(55.0f).epsilon(1e-4f));
    CHECK(sat_start(3.33333f) == doctest::Approx(73.3333f).epsilon(1e-4f));
    CHECK(sat_start(30.0f) == doctest::Approx(100.0f).epsilon(1e-4f));
    CHECK(sat_start(40.0f) == doctest::Approx(100.0f).epsilon(1e-4f));
    CHECK(sat_start(-80.0f) == doctest::Approx(0.0f).epsilon(1e-4f));
}

TEST_CASE("§7.5 comfort_clamp: комфорт от минус пяти до десяти") {
    CHECK(comfort_clamp(3.0f) == doctest::Approx(3.0f).epsilon(1e-4f));
    CHECK(comfort_clamp(10.0f) == doctest::Approx(10.0f).epsilon(1e-4f));
    CHECK(comfort_clamp(12.0f) == doctest::Approx(10.0f).epsilon(1e-4f));
    CHECK(comfort_clamp(-5.0f) == doctest::Approx(-5.0f).epsilon(1e-4f));
    CHECK(comfort_clamp(-7.0f) == doctest::Approx(-5.0f).epsilon(1e-4f));
}

TEST_CASE("§7.5 environment_clamp: окружение от минус десяти до пяти") {
    CHECK(environment_clamp(2.0f) == doctest::Approx(2.0f).epsilon(1e-4f));
    CHECK(environment_clamp(5.0f) == doctest::Approx(5.0f).epsilon(1e-4f));
    CHECK(environment_clamp(8.0f) == doctest::Approx(5.0f).epsilon(1e-4f));
    CHECK(environment_clamp(-10.0f) == doctest::Approx(-10.0f).epsilon(1e-4f));
    CHECK(environment_clamp(-12.0f) == doctest::Approx(-10.0f).epsilon(1e-4f));
}

TEST_CASE("§7.5 sat_delta_hour: сумма без обрезки и вклад каждой просьбы") {
    CHECK(sat_delta_hour(0.0f, 0.0f, 0.0f, 0, 0, 0.0f)
          == doctest::Approx(0.0f).epsilon(1e-4f));
    CHECK(sat_delta_hour(-6.0f, 4.0f, -2.0f, 1, 1, 0.0f)
          == doctest::Approx(-9.0f).epsilon(1e-4f));
    CHECK(sat_delta_hour(5.0f, 10.0f, 5.0f, 0, 2, 3.0f)
          == doctest::Approx(33.0f).epsilon(1e-4f));
    CHECK(sat_delta_hour(-12.0f, -5.0f, -10.0f, 3, 0, -5.0f)
          == doctest::Approx(-62.0f).epsilon(1e-4f));

    for (int requests = 0; requests < 5; ++requests) {
        CAPTURE(requests);
        const float unresolved_before = sat_delta_hour(-6.0f, 4.0f, -2.0f, requests, 1, 0.0f);
        const float unresolved_after = sat_delta_hour(-6.0f, 4.0f, -2.0f, requests + 1, 1, 0.0f);
        CHECK(unresolved_after - unresolved_before == doctest::Approx(-10.0f).epsilon(1e-4f));
        const float resolved_before = sat_delta_hour(-6.0f, 4.0f, -2.0f, 1, requests, 0.0f);
        const float resolved_after = sat_delta_hour(-6.0f, 4.0f, -2.0f, 1, requests + 1, 0.0f);
        CHECK(resolved_after - resolved_before == doctest::Approx(5.0f).epsilon(1e-4f));
    }
}

TEST_CASE("§7.5 leaves_early: уход строго ниже двадцати") {
    CHECK(leaves_early(19.9f) == true);
    CHECK(leaves_early(20.0f) == false);
    CHECK(leaves_early(0.0f) == true);
    CHECK(leaves_early(55.0f) == false);
}

TEST_CASE("§7.5 extend_prob: деньги, границы удовлетворённости и монотонность") {
    CHECK(extend_prob(100.0f, 0.7f, 20, 20) == doctest::Approx(0.7f).epsilon(1e-4f));
    CHECK(extend_prob(50.0f, 0.9f, 100, 20) == doctest::Approx(0.225f).epsilon(1e-4f));
    CHECK(extend_prob(80.0f, 0.7f, 40, 20) == doctest::Approx(0.448f).epsilon(1e-4f));
    CHECK(extend_prob(80.0f, 0.5f, 19, 20) == doctest::Approx(0.0f).epsilon(1e-6f));
    CHECK(extend_prob(120.0f, 0.9f, 50, 20) == doctest::Approx(0.9f).epsilon(1e-4f));
    CHECK(extend_prob(-10.0f, 0.9f, 50, 20) == doctest::Approx(0.0f).epsilon(1e-6f));

    for (int sat = 0; sat < 100; ++sat) {
        CAPTURE(sat);
        CHECK(extend_prob(static_cast<float>(sat + 1), 0.9f, 100, 20)
              >= extend_prob(static_cast<float>(sat), 0.9f, 100, 20));
    }
}

TEST_CASE("§7.5 leave_rep_delta: вес посетителя и нейтральная удовлетворённость") {
    CHECK(leave_rep_delta(0.3f, 55.0f) == doctest::Approx(0.0f).epsilon(1e-4f));
    CHECK(leave_rep_delta(0.3f, 100.0f) == doctest::Approx(0.3f).epsilon(1e-4f));
    CHECK(leave_rep_delta(0.3f, 10.0f) == doctest::Approx(-0.3f).epsilon(1e-4f));
    CHECK(leave_rep_delta(0.5f, 100.0f) == doctest::Approx(0.5f).epsilon(1e-4f));
    CHECK(leave_rep_delta(0.5f, 77.5f) == doctest::Approx(0.25f).epsilon(1e-4f));
    CHECK(leave_rep_delta(0.3f, 0.0f) == doctest::Approx(-0.366667f).epsilon(1e-4f));
}

TEST_CASE("§7.5 relation_delta: изменение отношений постоянного клиента") {
    CHECK(relation_delta(55.0f) == doctest::Approx(0.0f).epsilon(1e-4f));
    CHECK(relation_delta(75.0f) == doctest::Approx(2.0f).epsilon(1e-4f));
    CHECK(relation_delta(15.0f) == doctest::Approx(-4.0f).epsilon(1e-4f));
    CHECK(relation_delta(100.0f) == doctest::Approx(4.5f).epsilon(1e-4f));
}

TEST_CASE("§7.5 regular_eligible: три визита и строго больше шестидесяти") {
    CHECK(regular_eligible(3, 61.0f) == true);
    CHECK(regular_eligible(10, 60.1f) == true);
    CHECK(regular_eligible(2, 90.0f) == false);
    CHECK(regular_eligible(3, 60.0f) == false);
}

TEST_CASE("§7.5 regular_convert_prob: вероятность стать постоянным") {
    CHECK(regular_convert_prob() == doctest::Approx(0.3f).epsilon(1e-4f));
}

TEST_CASE("§7.6 game_popularity: рост, спад, минимум и особые сроки") {
    struct Sample {
        int age;
        float expected;
    };
    const Sample counter_strike[] = {
        {-1, 0.0f}, {0, 0.5f}, {15, 0.75f}, {29, 0.983333f},
        {30, 1.0f}, {230, 0.707107f}, {430, 0.6f}, {1000, 0.6f}
    };
    for (const auto& sample : counter_strike) {
        CAPTURE(sample.age);
        CHECK(game_popularity(sample.age, 1.0f, 30, 400, 0.6f)
              == doctest::Approx(sample.expected).epsilon(1e-4f));
    }
    const Sample shooter[] = {
        {3, 0.357143f}, {7, 0.5f}, {52, 0.25f}, {97, 0.125f}, {232, 0.025f}
    };
    for (const auto& sample : shooter) {
        CAPTURE(sample.age);
        CHECK(game_popularity(sample.age, 0.5f, 7, 45, 0.05f)
              == doctest::Approx(sample.expected).epsilon(1e-4f));
    }
    CHECK(game_popularity(0, 1.0f, 0, 100, 0.1f) == doctest::Approx(1.0f).epsilon(1e-4f));
    CHECK(game_popularity(100, 1.0f, 0, 100, 0.1f) == doctest::Approx(0.5f).epsilon(1e-4f));
    CHECK(game_popularity(10, 1.0f, 5, 0, 0.2f) == doctest::Approx(0.2f).epsilon(1e-4f));

    struct Game {
        float peak;
        int rise;
        int half_life;
        float floor;
    };
    const Game games[] = {{1.0f, 30, 400, 0.6f}, {0.5f, 7, 45, 0.05f}};
    for (const auto& game : games) {
        CAPTURE(game.peak);
        for (int age = 0; age <= game.rise + 2000; ++age) {
            CAPTURE(age);
            const float value = game_popularity(age, game.peak, game.rise, game.half_life, game.floor);
            CHECK(value <= game.peak);
            if (age >= game.rise) {
                CHECK(value >= game.peak * game.floor);
            }
            if (age > 0) {
                const float previous = game_popularity(age - 1, game.peak, game.rise,
                                                       game.half_life, game.floor);
                if (age <= game.rise) {
                    CHECK(value >= previous);
                } else {
                    CHECK(value <= previous);
                }
            }
        }
    }
}

TEST_CASE("§7.6 library_appeal: только пять лучших и независимость от порядка") {
    const float half[] = {0.5f};
    const float released_pair[] = {1.0f, 0.5f};
    const float zeroes[] = {0.0f, 0.0f};
    const float same[] = {0.9f, 0.8f, 0.7f, 0.6f, 0.5f, 0.4f};
    const float installed[] = {0.9f, 0.4f};
    const float released[] = {0.9f, 0.8f, 0.7f, 0.6f, 0.5f, 0.4f, 0.3f};
    const float released_reversed[] = {0.3f, 0.4f, 0.5f, 0.6f, 0.7f, 0.8f, 0.9f};
    const float small[] = {0.1f, 0.1f, 0.1f, 0.1f, 0.1f, 0.1f, 0.1f};
    const float many[] = {1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 0.1f,
                          0.1f, 0.1f, 0.1f, 0.1f, 0.1f, 0.1f};
    const float two[] = {2.0f};
    const float one[] = {1.0f};

    CHECK(library_appeal(half, 0, released_pair, 2) == doctest::Approx(0.0f).epsilon(1e-4f));
    CHECK(library_appeal(half, 1, released_pair, 0) == doctest::Approx(0.0f).epsilon(1e-4f));
    CHECK(library_appeal(half, 1, zeroes, 2) == doctest::Approx(0.0f).epsilon(1e-4f));
    CHECK(library_appeal(same, 6, same, 6) == doctest::Approx(1.0f).epsilon(1e-4f));
    CHECK(library_appeal(installed, 2, released, 7) == doctest::Approx(0.371429f).epsilon(1e-4f));
    CHECK(library_appeal(installed, 2, released_reversed, 7)
          == doctest::Approx(0.371429f).epsilon(1e-4f));
    CHECK(library_appeal(small, 7, many, 12) == doctest::Approx(0.1f).epsilon(1e-4f));
    CHECK(library_appeal(two, 1, one, 1) == doctest::Approx(1.0f).epsilon(1e-4f));
}

TEST_CASE("§7.7 dust_per_hour: множители окружения") {
    CHECK(dust_per_hour(1.0f) == doctest::Approx(1.0f).epsilon(1e-4f));
    CHECK(dust_per_hour(1.5f) == doctest::Approx(1.5f).epsilon(1e-4f));
    CHECK(dust_per_hour(0.6f) == doctest::Approx(0.6f).epsilon(1e-4f));
    CHECK(dust_per_hour(0.9f) == doctest::Approx(0.9f).epsilon(1e-4f));
}

TEST_CASE("§7.7 overheat: пыль, кондиционер и монотонность") {
    CHECK(overheat(0.0f, false, 0.5f) == doctest::Approx(0.0f).epsilon(1e-4f));
    CHECK(overheat(40.0f, false, 0.5f) == doctest::Approx(0.0f).epsilon(1e-4f));
    CHECK(overheat(55.0f, false, 0.5f) == doctest::Approx(0.25f).epsilon(1e-4f));
    CHECK(overheat(70.0f, false, 0.5f) == doctest::Approx(0.5f).epsilon(1e-4f));
    CHECK(overheat(100.0f, false, 0.5f) == doctest::Approx(1.0f).epsilon(1e-4f));
    CHECK(overheat(130.0f, false, 0.5f) == doctest::Approx(1.0f).epsilon(1e-4f));
    CHECK(overheat(70.0f, true, 0.5f) == doctest::Approx(0.25f).epsilon(1e-4f));
    CHECK(overheat(100.0f, true, 0.5f) == doctest::Approx(0.5f).epsilon(1e-4f));

    for (bool aircon : {false, true}) {
        for (int dust = 0; dust < 150; ++dust) {
            CAPTURE(aircon);
            CAPTURE(dust);
            CHECK(overheat(static_cast<float>(dust + 1), aircon, 0.5f)
                  >= overheat(static_cast<float>(dust), aircon, 0.5f));
        }
    }
}

TEST_CASE("§7.7 wear_per_hour: перегрев ускоряет износ") {
    CHECK(wear_per_hour(0.4f, 0.0f) == doctest::Approx(0.4f).epsilon(1e-4f));
    CHECK(wear_per_hour(0.4f, 0.5f) == doctest::Approx(0.6f).epsilon(1e-4f));
    CHECK(wear_per_hour(0.05f, 1.0f) == doctest::Approx(0.1f).epsilon(1e-4f));
}

TEST_CASE("§7.7 fail_prob: куб износа, перегрев и полная поломка") {
    CHECK(fail_prob(0.05f, 50.0f, false, 0.0f) == doctest::Approx(0.00625f).epsilon(1e-6f));
    CHECK(fail_prob(0.05f, 99.9f, false, 0.0f) == doctest::Approx(0.0498502f).epsilon(1e-6f));
    CHECK(fail_prob(0.02f, 0.0f, true, 0.5f) == doctest::Approx(0.01f).epsilon(1e-6f));
    CHECK(fail_prob(0.02f, 50.0f, true, 0.5f) == doctest::Approx(0.0125f).epsilon(1e-6f));
    CHECK(fail_prob(0.02f, 50.0f, false, 0.5f) == doctest::Approx(0.0025f).epsilon(1e-6f));
    CHECK(fail_prob(0.04f, 0.0f, false, 1.0f) == doctest::Approx(0.0f).epsilon(1e-6f));
    CHECK(fail_prob(0.02f, 100.0f, false, 0.0f) == doctest::Approx(1.0f).epsilon(1e-6f));
    CHECK(fail_prob(0.02f, 150.0f, true, 1.0f) == doctest::Approx(1.0f).epsilon(1e-6f));

    for (bool cpu_or_gpu : {false, true}) {
        for (int wear = 0; wear < 99; ++wear) {
            CAPTURE(cpu_or_gpu);
            CAPTURE(wear);
            CHECK(fail_prob(0.02f, static_cast<float>(wear + 1), cpu_or_gpu, 0.5f)
                  >= fail_prob(0.02f, static_cast<float>(wear), cpu_or_gpu, 0.5f));
        }
    }
}

TEST_CASE("§7.7 hang_prob: перегрев и вирус увеличивают вероятность зависания") {
    CHECK(hang_prob(0.0f, false) == doctest::Approx(0.02f).epsilon(1e-4f));
    CHECK(hang_prob(1.0f, false) == doctest::Approx(0.07f).epsilon(1e-4f));
    CHECK(hang_prob(0.5f, true) == doctest::Approx(0.145f).epsilon(1e-4f));
    CHECK(hang_prob(1.0f, true) == doctest::Approx(0.17f).epsilon(1e-4f));
}

TEST_CASE("§7.7 wear_after_repair: качество ремонта и граница нуля") {
    CHECK(wear_after_repair(80.0f, 0.0f) == doctest::Approx(50.0f).epsilon(1e-4f));
    CHECK(wear_after_repair(80.0f, 0.5f) == doctest::Approx(35.0f).epsilon(1e-4f));
    CHECK(wear_after_repair(80.0f, 1.0f) == doctest::Approx(20.0f).epsilon(1e-4f));
    CHECK(wear_after_repair(40.0f, 1.0f) == doctest::Approx(0.0f).epsilon(1e-4f));
    CHECK(wear_after_repair(80.0f, 1.5f) == doctest::Approx(20.0f).epsilon(1e-4f));
    CHECK(wear_after_repair(80.0f, -1.0f) == doctest::Approx(50.0f).epsilon(1e-4f));
}

TEST_CASE("§7.7 wear_broken: сломан начиная со ста") {
    CHECK(wear_broken(99.9f) == false);
    CHECK(wear_broken(100.0f) == true);
    CHECK(wear_broken(150.0f) == true);
}

TEST_CASE("§7.9 item_price: возраст, курс, дефицит и округление") {
    CHECK(item_price(3000, 0, 1.0f, 1.15f) == 3450);
    CHECK(item_price(3000, 0, 1.0f, 1.0f) == 3000);
    CHECK(item_price(6000, 1, 1.0f, 1.0f) == 5820);
    CHECK(item_price(3000, 12, 1.0f, 1.0f) == 2082);
    CHECK(item_price(2500, 0, 1.25f, 1.0f) == 3125);
    CHECK(item_price(3000, -3, 1.0f, 1.0f) == 3000);
}

TEST_CASE("§7.9 supply_factor: дефицит только в первый месяц") {
    CHECK(supply_factor(0) == doctest::Approx(1.15f).epsilon(1e-4f));
    CHECK(supply_factor(1) == doctest::Approx(1.0f).epsilon(1e-4f));
    CHECK(supply_factor(5) == doctest::Approx(1.0f).epsilon(1e-4f));
}

TEST_CASE("§7.9 used_price: половина цены с учётом износа") {
    CHECK(used_price(3000, 0.0f) == 1500);
    CHECK(used_price(3000, 50.0f) == 750);
    CHECK(used_price(1000, 25.0f) == 375);
    CHECK(used_price(1001, 0.0f) == 501);
    CHECK(used_price(3000, 100.0f) == 0);
    CHECK(used_price(3000, 150.0f) == 0);
    CHECK(used_price(3000, -10.0f) == 1500);
}

TEST_CASE("§7.13 buy_prob: голод, наценка, обрезка и монотонность") {
    CHECK(buy_prob(0.2f, 1.5f) == doctest::Approx(0.02f).epsilon(1e-4f));
    CHECK(buy_prob(0.2f, 1.0f) == doctest::Approx(0.12f).epsilon(1e-4f));
    CHECK(buy_prob(0.1f, 0.4f) == doctest::Approx(0.12f).epsilon(1e-4f));
    CHECK(buy_prob(0.2f, 0.2f) == doctest::Approx(0.24f).epsilon(1e-4f));
    CHECK(buy_prob(0.3f, 1.6f) == doctest::Approx(0.0f).epsilon(1e-6f));
    CHECK(buy_prob(0.3f, 2.0f) == doctest::Approx(0.0f).epsilon(1e-6f));

    for (int step = 0; step < 60; ++step) {
        CAPTURE(step);
        const float markup = static_cast<float>(step) / 20.0f;
        const float next_markup = static_cast<float>(step + 1) / 20.0f;
        CHECK(buy_prob(0.2f, next_markup) <= buy_prob(0.2f, markup));
    }
}

TEST_CASE("§7.13 goods_missing_sat_penalty: нерешённая просьба о товаре") {
    CHECK(goods_missing_sat_penalty() == doctest::Approx(-5.0f).epsilon(1e-4f));
}

TEST_CASE("§7.14 electricity_cost: нагрузка, часы работы и округление рублей") {
    CHECK(electricity_cost(49.5f, 15.0f, 1.2f) == 24);
    CHECK(electricity_cost(54.0f, 15.0f, 1.2f) == 25);
    CHECK(electricity_cost(10.0f, 10.0f, 1.0f) == 8);
    CHECK(electricity_cost(0.0f, 0.0f, 1.2f) == 0);
}

TEST_CASE("§7.14 tax: налог только с положительной выручки") {
    CHECK(tax(29700, 0.06f) == 1782);
    CHECK(tax(10050, 0.06f) == 603);
    CHECK(tax(0, 0.06f) == 0);
    CHECK(tax(-500, 0.06f) == 0);
}

TEST_CASE("§7.14 loan_interest: долг и округление процентов") {
    CHECK(loan_interest(10000, 0.1f) == 1000);
    CHECK(loan_interest(12345, 0.1f) == 1235);
    CHECK(loan_interest(0, 0.1f) == 0);
    CHECK(loan_interest(-500, 0.1f) == 0);
}

TEST_CASE("§7.15 rep_after_daily_drift: границы и сходимость к сорока") {
    CHECK(rep_after_daily_drift(30.0f) == doctest::Approx(30.05f).epsilon(1e-4f));
    CHECK(rep_after_daily_drift(40.0f) == doctest::Approx(40.0f).epsilon(1e-4f));
    CHECK(rep_after_daily_drift(50.0f) == doctest::Approx(49.95f).epsilon(1e-4f));
    CHECK(rep_after_daily_drift(100.0f) == doctest::Approx(99.7f).epsilon(1e-4f));
    CHECK(rep_after_daily_drift(0.0f) == doctest::Approx(0.2f).epsilon(1e-4f));
    CHECK(rep_after_daily_drift(-10.0f) == doctest::Approx(0.0f).epsilon(1e-4f));
    CHECK(rep_after_daily_drift(150.0f) == doctest::Approx(100.0f).epsilon(1e-4f));

    float from_below = 30.0f;
    float from_above = 100.0f;
    for (int day = 0; day < 1000; ++day) {
        CAPTURE(day);
        const float next_below = rep_after_daily_drift(from_below);
        const float next_above = rep_after_daily_drift(from_above);
        CHECK(next_below >= from_below);
        CHECK(next_above <= from_above);
        from_below = next_below;
        from_above = next_above;
    }
    CHECK(std::fabs(from_below - 40.0f) < 0.5f);
    CHECK(std::fabs(from_above - 40.0f) < 0.5f);
}

TEST_CASE("§7.15 rep_clamp: диапазон репутации") {
    CHECK(rep_clamp(-5.0f) == doctest::Approx(0.0f).epsilon(1e-4f));
    CHECK(rep_clamp(50.0f) == doctest::Approx(50.0f).epsilon(1e-4f));
    CHECK(rep_clamp(120.0f) == doctest::Approx(100.0f).epsilon(1e-4f));
}
