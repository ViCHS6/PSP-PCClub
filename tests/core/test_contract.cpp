#include <doctest/doctest.h>

#include <type_traits>

#include "core/core_api.h"
#include "core/data_api.h"
#include "core/data_types.h"
#include "core/formulas_api.h"
#include "core/rng_api.h"

using namespace core;

// World копируется memcpy (снимки в balance-runner, сохранение).
static_assert(std::is_trivially_copyable<World>::value, "World");
static_assert(std::is_trivially_copyable<GameData>::value, "GameData");
// Бюджет: мир целиком — статическая память, держим его в пределах 32 КиБ.
static_assert(sizeof(World) <= 32 * 1024, "World слишком большой");

TEST_CASE("даты: эпоха 2000-01-01, старт игры, обратное преобразование") {
    CHECK(date_from_ymd(2000, 1, 1) == 0);
    CHECK(date_from_ymd(2001, 9, 1) == 609);
    CHECK(date_from_ymd(1999, 12, 31) == -1);
    CHECK(date_from_ymd(2004, 3, 1) - date_from_ymd(2004, 2, 28) == 2);  // високосный

    for (DayNum d = -1000; d < 3000; d += 37) {
        const Ymd ymd = ymd_from_date(d);
        CHECK(date_from_ymd(ymd.year, ymd.month, ymd.day) == d);
    }
    CHECK(month_day_of(date_from_ymd(2001, 11, 3)) == 1103);
}

TEST_CASE("дни недели: 1 сентября 2001 — суббота") {
    CHECK(weekday_of(0) == 5);                                 // 2000-01-01 — суббота
    CHECK(weekday_of(date_from_ymd(2001, 9, 1)) == 5);         // GDD §4
    CHECK(weekday_of(date_from_ymd(2001, 9, 3)) == 0);         // понедельник
    CHECK(weekday_of(date_from_ymd(1998, 1, 1)) == 3);         // четверг, отрицательный DayNum
}

TEST_CASE("ГПСЧ: детерминирован и в заявленных диапазонах") {
    Rng a, b;
    rng_seed(a, 12345);
    rng_seed(b, 12345);
    for (int i = 0; i < 100; ++i) CHECK(rng_next(a) == rng_next(b));

    Rng z;
    rng_seed(z, 0);
    CHECK(z.state != 0u);

    Rng r;
    rng_seed(r, 1);
    CHECK(rng_next(r) == 270369u);  // xorshift32 13/17/5 от 1 — фиксирует алгоритм
    for (int i = 0; i < 1000; ++i) {
        const float f = rng_float01(r);
        CHECK((f >= 0.0f && f < 1.0f));
        const int32_t v = rng_range(r, 30, 120);
        CHECK((v >= 30 && v <= 120));
    }
    CHECK(rng_range(r, 5, 5) == 5);
    CHECK_FALSE(rng_chance(r, 0.0f));
    CHECK(rng_chance(r, 1.0f));
}

TEST_CASE("FNV-1a: эталонные значения") {
    CHECK(fnv1a32("") == 2166136261u);
    CHECK(fnv1a32("a") == 0xE40C292Cu);
}
