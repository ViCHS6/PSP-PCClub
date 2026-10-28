// Хранилище сохранения на PSP (контракт — platform/save_api.h): системная утилита savedata.
// Один слот: SAVEDATA/PCCB00001SAVE/DATA.BIN без шифрования. Носитель (ms0:/ef0:) выбирает
// система — на Go это внутренняя память.
#include <pspdisplay.h>
#include <pspgu.h>
#include <psputility.h>

#include <cstdio>
#include <cstring>

#include "platform/save_api.h"

namespace platform {

namespace {

constexpr char kGameName[] = "PCCB00001";  // 9 символов — как код диска у коммерческих игр
constexpr char kSaveName[] = "SAVE";
constexpr char kFileName[] = "DATA.BIN";

// Коды base.result утилиты (как в PPSSPP, SavedataParam.h); прочие ошибки — kError.
constexpr unsigned kErrLoadNoData = 0x80110307u;
constexpr unsigned kErrSaveNoSpace = 0x80110383u;
constexpr int kResultCancelled = 1;

constexpr unsigned kBackground = 0xFF201C18u;  // тёмный фон под диалогом, ABGR

alignas(16) uint32_t g_list[64];
SceUtilitySavedataParam g_param;

void copy(char* dst, size_t cap, const char* src) { std::snprintf(dst, cap, "%s", src ? src : ""); }

void prepare(PspUtilitySavedataMode mode, void* buf, size_t buf_size, size_t data_size) {
    std::memset(&g_param, 0, sizeof(g_param));
    g_param.base.size = sizeof(g_param);
    sceUtilityGetSystemParamInt(PSP_SYSTEMPARAM_ID_INT_LANGUAGE, &g_param.base.language);
    sceUtilityGetSystemParamInt(PSP_SYSTEMPARAM_ID_INT_UNKNOWN, &g_param.base.buttonSwap);
    // Приоритеты потоков утилиты — как в примерах pspsdk.
    g_param.base.graphicsThread = 0x11;
    g_param.base.accessThread = 0x13;
    g_param.base.fontThread = 0x12;
    g_param.base.soundThread = 0x10;
    g_param.mode = mode;
    g_param.overwrite = 1;
    g_param.focus = PSP_UTILITY_SAVEDATA_FOCUS_LATEST;
    copy(g_param.gameName, sizeof(g_param.gameName), kGameName);
    copy(g_param.saveName, sizeof(g_param.saveName), kSaveName);
    copy(g_param.fileName, sizeof(g_param.fileName), kFileName);
    g_param.dataBuf = buf;
    g_param.dataBufSize = static_cast<SceSize>(buf_size);
    g_param.dataSize = static_cast<SceSize>(data_size);
}

// Крутит кадры, пока утилита не закончит. dialog — утилита рисует окно поверх нашего фона;
// иначе (AUTO-режимы, окна нет) экран не трогаем — остаётся последний кадр игры.
int run(bool dialog) {
    if (sceUtilitySavedataInitStart(&g_param) < 0) return -1;
    for (;;) {
        if (dialog) {
            sceGuStart(GU_DIRECT, g_list);
            sceGuClearColor(kBackground);
            sceGuClear(GU_COLOR_BUFFER_BIT);
            sceGuFinish();
            sceGuSync(0, 0);
        }
        const int status = sceUtilitySavedataGetStatus();
        if (status == PSP_UTILITY_DIALOG_VISIBLE) {
            sceUtilitySavedataUpdate(1);
        } else if (status == PSP_UTILITY_DIALOG_QUIT) {
            sceUtilitySavedataShutdownStart();
        } else if (status == PSP_UTILITY_DIALOG_NONE || status < 0) {
            break;
        }
        sceDisplayWaitVblankStart();
        if (dialog) sceGuSwapBuffers();
    }
    return g_param.base.result;
}

SaveResult to_result(int code) {
    if (code == 0) return SaveResult::kOk;
    if (code == kResultCancelled) return SaveResult::kCancelled;
    const unsigned u = static_cast<unsigned>(code);
    if (u == kErrLoadNoData) return SaveResult::kNoSave;
    if (u == kErrSaveNoSpace) return SaveResult::kNoSpace;
    return SaveResult::kError;
}

}  // namespace

SaveResult save_write(const void* data, size_t size, const SaveMeta& meta, SaveUi ui) {
    if (size > kSaveCapacity) return SaveResult::kTooBig;
    if (!data) return SaveResult::kError;
    const bool dialog = ui == SaveUi::kDialog;
    // Утилита буфер только читает; const снимаем ради её структуры.
    prepare(dialog ? PSP_UTILITY_SAVEDATA_SAVE : PSP_UTILITY_SAVEDATA_AUTOSAVE, const_cast<void*>(data),
            size, size);
    copy(g_param.sfoParam.title, sizeof(g_param.sfoParam.title), meta.title);
    copy(g_param.sfoParam.savedataTitle, sizeof(g_param.sfoParam.savedataTitle), meta.title);
    copy(g_param.sfoParam.detail, sizeof(g_param.sfoParam.detail), meta.detail);
    g_param.sfoParam.parentalLevel = 1;
    return to_result(run(dialog));
}

SaveResult save_read(void* dst, size_t capacity, size_t* out_size, SaveUi ui) {
    if (out_size) *out_size = 0;
    if (!dst) return SaveResult::kError;
    const bool dialog = ui == SaveUi::kDialog;
    prepare(dialog ? PSP_UTILITY_SAVEDATA_LOAD : PSP_UTILITY_SAVEDATA_AUTOLOAD, dst, capacity, 0);
    const SaveResult result = to_result(run(dialog));
    if (result != SaveResult::kOk) return result;
    if (g_param.dataSize > capacity) return SaveResult::kTooBig;
    if (out_size) *out_size = g_param.dataSize;
    return SaveResult::kOk;
}

}  // namespace platform
