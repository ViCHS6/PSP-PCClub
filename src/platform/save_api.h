#pragma once
// КОНТРАКТ (T060). Хранилище сохранения: один слот, байты как есть (их формат — забота
// core/save_api.h и docs/formats/save.md §6). Реализации: psp/save_psp.cpp — системная
// утилита savedata (sceUtilitySavedata, папка SAVEDATA/PCCB00001SAVE), pc/save_pc.cpp —
// файл save.bin рядом с приложением. Менять — только задачей «меняет контракт».
//
// Вызывать из главного потока МЕЖДУ кадрами: после render::end_frame и до следующего
// render::begin_frame. Функции блокируют до конца операции. PSP: пока работает утилита,
// функция сама крутит кадры (тёмный фон + системный диалог, vblank) — мир, UI и мини-игры
// в это время не обновляются, аудиопоток играет. kSilent на PSP — без вопросов игроку
// (на экране только системный значок записи), kDialog — подтверждение «Сохранить?»/«Загрузить?».
// PC: SaveUi игнорируется, операция мгновенная.
//
// Без аллокаций; буферы — у вызывающего.
//
//     platform::SaveMeta meta{"Компьютерный клуб", "День 12 · 12.09.2001 · 3 450 руб."};
//     platform::save_write(buf, size, meta, platform::SaveUi::kSilent);
#include <cstddef>
#include <cstdint>

namespace platform {

constexpr size_t kSaveCapacity = 64 * 1024;  // больше записать нельзя (= core::kSaveMaxSize)

enum class SaveUi : uint8_t {
    kSilent,  // автосохранение и автозагрузка
    kDialog,  // игрок явно выбрал «Сохранить»/«Загрузить»
};

enum class SaveResult : uint8_t {
    kOk,
    kNoSave,     // save_read: сохранения нет
    kCancelled,  // игрок отменил системный диалог (только kDialog)
    kNoSpace,    // нет места на носителе
    kTooBig,     // size > kSaveCapacity (write) или файл больше capacity (read)
    kError,      // нет носителя, ошибка доступа, повреждённые данные savedata
};

// Подписи для системного списка сохранений PSP (UTF-8, кириллица — как в EBOOT).
// Строки — из strings_ru.json. PC игнорирует.
struct SaveMeta {
    const char* title = "";   // название игры (≤ 127 байт)
    const char* detail = "";  // описание слота: день, дата, деньги (≤ 1023 байт)
};

// Записывает size байт в слот, заменяя прежнее сохранение. PC — атомарно (временный
// файл + переименование): при сбое старое сохранение остаётся.
SaveResult save_write(const void* data, size_t size, const SaveMeta& meta, SaveUi ui);

// Читает слот в dst. kOk — *out_size = размер прочитанного.
SaveResult save_read(void* dst, size_t capacity, size_t* out_size, SaveUi ui);

}  // namespace platform
