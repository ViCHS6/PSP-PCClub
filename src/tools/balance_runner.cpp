#include <charconv>
#include <chrono>
#include <cinttypes>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <initializer_list>
#include <limits>
#include <system_error>

#include "balance_bots.h"
#include "core/core_api.h"
#include "core/data_api.h"

namespace {

struct Options {
    int32_t days = 90;
    uint32_t seed = 1;
    balance::Strategy strategy = balance::Strategy::kNormal;
    std::filesystem::path data;
    std::filesystem::path out = "balance.csv";
};

void usage() {
    std::puts("balance-runner [--days N] [--seed S] [--strategy passive|normal|aggressive]\n"
              "               [--data path/data.pak] [--out file.csv]\n"
              "Defaults: days=90, seed=1, strategy=normal, out=balance.csv;\n"
              "data=balance_test.pak next to the executable (test fixture, not game content).\n"
              "CSV: one row per completed day, measured at closing before cmd_end_day.\n"
              "load_pct = loaded PC minutes / all hall seat minutes while open;\n"
              "visitors_lost = unserved departures (no money/game, price, patience, full queue);\n"
              "satisfaction_avg = DayStats.sat_sum / served (0 if none).\n"
              "Game over ends the run early with a diagnostic and preserves completed days.");
}

template <typename T>
bool integer(const char* str, T& value) {
    const char* end = str + std::strlen(str);
    const auto result = std::from_chars(str, end, value);
    return result.ec == std::errc{} && result.ptr == end;
}

bool parse_options(int argc, char** argv, Options& options) {
    const char* names[] = {"--days", "--seed", "--strategy", "--data", "--out"};
    bool seen[5]{};
    options.data = std::filesystem::path(argv[0]).parent_path() / "balance_test.pak";
    for (int i = 1; i < argc; ++i) {
        int option = -1;
        for (int n = 0; n < 5; ++n) {
            if (std::strcmp(argv[i], names[n]) == 0) option = n;
        }
        if (option < 0) {
            std::fprintf(stderr, "Unknown argument: %s (see --help)\n", argv[i]);
            return false;
        }
        if (seen[option] || i + 1 == argc || argv[i + 1][0] == '\0') {
            std::fprintf(stderr, "Expected one nonempty value for %s, specified once.\n", argv[i]);
            return false;
        }
        seen[option] = true;
        const char* value = argv[++i];
        bool valid = true;
        switch (option) {
            case 0:
                valid = integer(value, options.days) && options.days > 0 &&
                        options.days < std::numeric_limits<int32_t>::max();
                break;
            case 1: valid = integer(value, options.seed); break;
            case 2:
                if (std::strcmp(value, "passive") == 0) options.strategy = balance::Strategy::kPassive;
                else if (std::strcmp(value, "normal") == 0) options.strategy = balance::Strategy::kNormal;
                else if (std::strcmp(value, "aggressive") == 0) options.strategy = balance::Strategy::kAggressive;
                else valid = false;
                break;
            case 3: options.data = value; break;
            case 4: options.out = value; break;
        }
        if (!valid) {
            std::fprintf(stderr, "Invalid value for %s: %s\n", names[option], value);
            return false;
        }
    }
    return true;
}

bool load_data(const std::filesystem::path& path, core::GameData& data) {
    // GDD §10.3: пак до 1 МиБ. Буфер живёт дольше World, выравнивание требует data_load.
    alignas(16) static uint8_t buffer[1 << 20];
    FILE* file = std::fopen(path.string().c_str(), "rb");
    if (!file) {
        std::fprintf(stderr, "Cannot open data pack: %s\n", path.string().c_str());
        return false;
    }
    const size_t size = std::fread(buffer, 1, sizeof(buffer), file);
    const bool too_large = std::fgetc(file) != EOF;
    const bool read_error = std::ferror(file) != 0;
    const bool close_error = std::fclose(file) != 0;
    if (read_error || close_error || too_large) {
        std::fprintf(stderr, "Cannot read data pack (maximum 1 MiB): %s\n", path.string().c_str());
        return false;
    }
    const core::DataError error = core::data_load(buffer, size, &data);
    if (error != core::DataError::kOk) {
        std::fprintf(stderr, "Invalid data pack %s: %s\n", path.string().c_str(), core::data_error_text(error));
        return false;
    }
    return true;
}

bool write_day(FILE* file, const core::World& w, int64_t capacity_minutes) {
    using namespace core;
    const DayStats& day = w.stats.today;
    const Ymd date = ymd_from_date(day.date);
    int64_t revenue = 0, expenses = 0;
    for (int32_t value : day.revenue) revenue += value;
    for (int32_t value : day.expenses) expenses += value;
    int lost = 0;
    for (LeaveReason reason : {LeaveReason::kNoMoney, LeaveReason::kNoGame, LeaveReason::kPrice,
                              LeaveReason::kPatience, LeaveReason::kQueueFull})
        lost += day.left[static_cast<int>(reason)];
    const double load = capacity_minutes > 0 ? 100.0 * day.load_minutes / capacity_minutes : 0.0;
    const double sat = day.served > 0 ? static_cast<double>(day.sat_sum) / day.served : 0.0;
    return std::fprintf(file,
        "%" PRId32 ",%04d-%02u-%02u,%" PRId32 ",%" PRId64 ",%" PRId64 ",%" PRId64
        ",%u,%u,%d,%.3f,%.3f,%.3f,%d,%u,%u,%" PRId32 "\n",
        w.clock.day_index, date.year, static_cast<unsigned>(date.month), static_cast<unsigned>(date.day),
        day.money_end, revenue, expenses, revenue - expenses,
        static_cast<unsigned>(day.arrived), static_cast<unsigned>(day.served), lost, load, sat,
        static_cast<double>(day.rep_end), pcs_working(w),
        static_cast<unsigned>(day.pc_hangs), static_cast<unsigned>(day.pc_failures), day.debt_end) >= 0;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc == 2 && std::strcmp(argv[1], "--help") == 0) { usage(); return 0; }
    Options options;
    if (!parse_options(argc, argv, options)) return 2;
    core::GameData data;
    if (!load_data(options.data, data)) return 1;
    std::error_code error;
    if (std::filesystem::equivalent(options.data, options.out, error)) {
        std::fprintf(stderr, "Output CSV must not overwrite the data pack.\n");
        return 2;
    }
    FILE* file = std::fopen(options.out.string().c_str(), "wb");
    if (!file) {
        std::fprintf(stderr, "Cannot open output CSV: %s\n", options.out.string().c_str());
        return 1;
    }
    bool ok = std::fputs("day,date,money,revenue,expenses,profit,visitors_arrived,visitors_served,"
                         "visitors_lost,load_pct,satisfaction_avg,reputation,pcs_working,"
                         "hung_events,broken_events,debt\n", file) >= 0;
    static core::World world;
    core::world_init(world, data, options.seed);
    balance::Bot bot;
    bot.strategy = options.strategy;
    int completed = 0;
    int64_t capacity_minutes = 0;
    const auto start = std::chrono::steady_clock::now();
    while (ok && completed < options.days && world.clock.phase != core::Phase::kGameOver) {
        if (world.clock.phase == core::Phase::kClosed) {
            ok = write_day(file, world, capacity_minutes);
            ++completed;
            capacity_minutes = 0;
            if (completed == options.days) break;
            ok = ok && core::cmd_end_day(world) == core::CmdResult::kOk;
            continue;
        }
        balance::bot_act(bot, world);
        if (world.clock.phase == core::Phase::kOpen) capacity_minutes += world.club.seats;
        core::tick(world);
    }
    if (std::fclose(file) != 0) ok = false;
    if (!ok) {
        std::fprintf(stderr, "Run or CSV write failed: %s\n", options.out.string().c_str());
        return 1;
    }
    if (completed < options.days)
        std::fprintf(stderr, "Game over: completed %d of %" PRId32 " days; CSV ends here.\n", completed, options.days);
    const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    std::printf("Completed %d days in %.3f s: %s\n", completed, seconds, options.out.string().c_str());
    return 0;
}
