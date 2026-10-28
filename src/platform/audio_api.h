#pragma once
// КОНТРАКТ (T061). Звук игры: программный микшер поверх platform::audio_start().
// GDD §10.3: один музыкальный поток (радио у стойки) + до 8 эффектов одновременно.
// Реализация: src/platform/audio_mixer.cpp (общая для PSP и PC), вывод — platform_*.cpp.
//
// Потоки. Все функции ниже вызываются ТОЛЬКО из главного потока. Микширование идёт в
// аудиопотоке (колбэк platform::audio_start). Обмен между ними без блокировок: команды —
// очередь фиксированного размера «один пишет, один читает», музыка — кольцевой буфер с
// атомарными индексами. Аудиопоток не выделяет память, не ждёт мьютексов и не читает файлы.
//
// Память. Всё выделяется в init(): арена PCM эффектов, кольцевой буфер музыки, буфер чтения.
// Удаления звуков по одному нет — только всё сразу в shutdown(). В кадре память не выделяется.
//
// Нет устройства — init() всё равно возвращает true: звуки грузятся, play() отдаёт
// дескрипторы, просто ничего не слышно. Игра не обязана проверять наличие звука.
//
// Пример:
//
//     audio::init(audio::Config{});                      // после platform::init()
//     const audio::SoundId click = audio::sound_load_file("assets/sfx_click.wav");
//     audio::music_play("assets/music_radio1.wav", true);
//     while (platform::poll()) {
//         if (pressed) audio::play(click, 0.8f);
//         audio::update();                                // раз в кадр
//     }
//     audio::shutdown();
#include <cstddef>
#include <cstdint>

namespace audio {

constexpr int kMaxVoices = 8;  // одновременных эффектов (GDD §10.3)

struct Config {
    size_t sound_arena_bytes = 1024 * 1024;  // PCM всех эффектов в исходном формате файла
    uint16_t max_sounds = 64;
    size_t music_ring_bytes = 128 * 1024;    // кольцо музыки в исходном формате: ≈ 3 с для
                                             // 22 050 Гц моно, ≈ 0,7 с для 44 100 Гц стерео
};

// После platform::init(). Запускает вывод. false — неверный Config или не хватило памяти.
bool init(const Config& config);

// Останавливает вывод (platform::audio_stop), закрывает музыку, освобождает всё.
void shutdown();

// Раз в кадр: дочитывает музыку из файла в кольцо (platform::file_read_at), снимает статистику.
// Если кольцо опустело — тишина вместо музыки и stats().music_underruns += 1, без щелчка.
void update();

// ---------------------------------------------------------------------------
// Эффекты
//
// Формат файлов: WAV (RIFF), PCM 16 бит, моно или стерео, 11 025 / 22 050 / 44 100 Гц.
// Остальное (8 бит, float, ADPCM, другие частоты) — ошибка загрузки. Чанки кроме fmt и data
// пропускаются. Пересэмплирование до 44 100 Гц — линейное, при микшировании.

using SoundId = uint16_t;
constexpr SoundId kNoSound = 0xFFFF;

// name — ASCII 1..31 байт, обычно имя файла без расширения. Данные копируются в арену.
// kNoSound — неверный WAV, имя занято, кончилась арена или лимит; причина — в stderr.
// Только при загрузке, не в кадре.
SoundId sound_load_wav(const char* name, const void* data, size_t size);

// Обёртка над platform::file_read: временный буфер выделяется и освобождается внутри.
// Имя — basename пути без расширения.
SoundId sound_load_file(const char* path);

SoundId sound_find(const char* name);

// Длительность в миллисекундах; 0 — неверный id.
uint32_t sound_length_ms(SoundId sound);

// ---------------------------------------------------------------------------
// Голоса

// Дескриптор голоса: слот + поколение. Протухший дескриптор (голос доиграл или вытеснен)
// безопасно передавать в любую функцию — она ничего не делает.
using VoiceId = uint32_t;
constexpr VoiceId kNoVoice = 0;

// volume 0..1, pan −1 (лево) … +1 (право), равная мощность. Значения вне диапазона обрезаются.
// Все 8 голосов заняты — вытесняется самый старый голос без loop; если все с loop — kNoVoice.
// kNoVoice и для неверного sound. Изменения громкости плавные (≈ 5 мс), без щелчков.
VoiceId play(SoundId sound, float volume = 1.0f, float pan = 0.0f, bool loop = false);

void stop(VoiceId voice);       // с коротким затуханием (≈ 5 мс)
void set_volume(VoiceId voice, float volume);
void set_pan(VoiceId voice, float pan);

// По последнему известному главному потоку состоянию: true сразу после play(),
// false — после stop() или когда аудиопоток сообщил, что голос доиграл.
bool is_playing(VoiceId voice);

void stop_all();  // все эффекты, музыку не трогает

// ---------------------------------------------------------------------------
// Музыка — один поток, читается из файла кусками в update()

// WAV того же формата, что эффекты. Предыдущая музыка останавливается. Файл держится открытым
// (platform::file_open) до music_stop(). loop — с начала данных после конца.
// false — нет файла, неверный формат; причина — в stderr. Начало звучит после ближайших update().
bool music_play(const char* path, bool loop);
void music_stop();
bool music_playing();

// ---------------------------------------------------------------------------
// Громкости (программа «Настройки», GDD §9.2): 0..1, плавно.

void set_master_volume(float volume);
void set_sfx_volume(float volume);
void set_music_volume(float volume);

// ---------------------------------------------------------------------------
// Отладка (оверлей SELECT)

struct Stats {
    uint8_t voices_active = 0;
    uint32_t music_underruns = 0;    // раз кольцо музыки опустело во время воспроизведения
    uint32_t command_overflows = 0;  // очередь команд была полна, команда отброшена
    uint32_t mix_us_last = 0;        // время последнего вызова колбэка, мкс
    uint32_t mix_us_peak = 0;        // пик с init()
    size_t sound_arena_used = 0;
    size_t sound_arena_size = 0;
};

const Stats& stats();

}  // namespace audio
