#pragma once
// Только PC: параметры командной строки для автоматических проверок.
//
//   --frames N            выйти после N кадров
//   --screenshot путь.bmp сохранить последний кадр (без --frames — после 60 кадров)
namespace platform {

struct PcOptions {
    int max_frames = 0;                     // 0 — без ограничения
    const char* screenshot_path = nullptr;  // nullptr — без снимка
};

const PcOptions& pc_options();

}  // namespace platform
