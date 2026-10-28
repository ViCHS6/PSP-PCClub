// T060: хранилище сохранения на PC — save.bin рядом с приложением (без init — в текущей папке).
#include <doctest/doctest.h>

#include <cstdio>
#include <vector>

#include "platform/save_api.h"

TEST_CASE("save_pc: запись, чтение, замена, нет файла, слишком большой") {
    std::remove("save.bin");
    std::vector<uint8_t> buf(platform::kSaveCapacity);
    size_t size = 99;
    CHECK(platform::save_read(buf.data(), buf.size(), &size, platform::SaveUi::kSilent) ==
          platform::SaveResult::kNoSave);
    CHECK(size == 0);

    const platform::SaveMeta meta{"Клуб", "День 1"};
    std::vector<uint8_t> data(1000);
    for (size_t i = 0; i < data.size(); ++i) data[i] = static_cast<uint8_t>(i * 7);
    REQUIRE(platform::save_write(data.data(), data.size(), meta, platform::SaveUi::kSilent) ==
            platform::SaveResult::kOk);
    REQUIRE(platform::save_read(buf.data(), buf.size(), &size, platform::SaveUi::kDialog) ==
            platform::SaveResult::kOk);
    CHECK(size == data.size());
    CHECK(std::vector<uint8_t>(buf.begin(), buf.begin() + size) == data);

    data.resize(10, 0xAB);  // новое сохранение целиком заменяет старое
    REQUIRE(platform::save_write(data.data(), data.size(), meta, platform::SaveUi::kSilent) ==
            platform::SaveResult::kOk);
    REQUIRE(platform::save_read(buf.data(), buf.size(), &size, platform::SaveUi::kSilent) ==
            platform::SaveResult::kOk);
    CHECK(size == 10);
    CHECK(std::vector<uint8_t>(buf.begin(), buf.begin() + size) == data);
    CHECK(platform::save_read(buf.data(), 5, &size, platform::SaveUi::kSilent) == platform::SaveResult::kTooBig);

    std::vector<uint8_t> huge(platform::kSaveCapacity + 1);
    CHECK(platform::save_write(huge.data(), huge.size(), meta, platform::SaveUi::kSilent) ==
          platform::SaveResult::kTooBig);
    std::remove("save.bin");
}
