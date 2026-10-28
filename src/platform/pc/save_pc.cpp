// Хранилище сохранения на PC (контракт — platform/save_api.h): save.bin рядом с приложением.
#include <cstdio>

#include "platform/pc/pc_options.h"
#include "platform/platform_api.h"
#include "platform/save_api.h"

namespace platform {

namespace {

constexpr const char* kSaveFile = "save.bin";
constexpr const char* kTempFile = "save.tmp";

}  // namespace

SaveResult save_write(const void* data, size_t size, const SaveMeta&, SaveUi) {
    if (size > kSaveCapacity) return SaveResult::kTooBig;
    if (!pc_options().use_save) return SaveResult::kOk;
    char tmp[1024], dst[1024];
    if (!data || !pc_full_path(kTempFile, tmp, sizeof(tmp)) || !pc_full_path(kSaveFile, dst, sizeof(dst))) {
        return SaveResult::kError;
    }
    FILE* f = std::fopen(tmp, "wb");
    if (!f) return SaveResult::kError;
    const bool written = std::fwrite(data, 1, size, f) == size;
    if (std::fclose(f) != 0 || !written) {
        std::remove(tmp);
        return SaveResult::kError;
    }
    // POSIX rename заменяет save.bin атомарно: при сбое остаётся прежнее сохранение.
    if (std::rename(tmp, dst) != 0) {
        std::remove(tmp);
        return SaveResult::kError;
    }
    return SaveResult::kOk;
}

SaveResult save_read(void* dst, size_t capacity, size_t* out_size, SaveUi) {
    if (out_size) *out_size = 0;
    size_t size = 0;
    if (!pc_options().use_save || !file_size(kSaveFile, &size)) return SaveResult::kNoSave;
    if (size > capacity) return SaveResult::kTooBig;
    if (!file_read(kSaveFile, dst, capacity, out_size)) return SaveResult::kError;
    return SaveResult::kOk;
}

}  // namespace platform
