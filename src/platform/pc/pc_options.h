#pragma once
// Только PC: параметры командной строки для автоматических проверок.
//
//   --frames N            выйти после N кадров
//   --screenshot путь.bmp сохранить последний кадр (без --frames — после 60 кадров)
//   --save                с --frames всё же читать и писать save.bin (иначе прогоны
//                         для снимков сохранение не трогают и воспроизводимы)
#include <cstddef>

namespace platform {

struct PcOptions {
    int max_frames = 0;                     // 0 — без ограничения
    const char* screenshot_path = nullptr;  // nullptr — без снимка
    bool use_save = true;                   // false — save_read «нет сохранения», save_write ничего не пишет
};

const PcOptions& pc_options();

// Полный путь файла рядом с приложением (правила путей — platform_api.h). false — путь
// недопустим или не влез в capacity.
bool pc_full_path(const char* path, char* out, size_t capacity);

}  // namespace platform
