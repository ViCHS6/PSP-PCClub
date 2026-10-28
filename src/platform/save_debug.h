#pragma once
// DBG: последний сырой код хранилища — для строки состояния «Отладки». НЕ контракт.
// PSP — base.result утилиты savedata (0 — успех, 1 — отмена, 0x8011xxxx — ошибка) или код
// sceUtilitySavedataInitStart; PC — всегда 0.
namespace platform {
int save_last_code();
}  // namespace platform
