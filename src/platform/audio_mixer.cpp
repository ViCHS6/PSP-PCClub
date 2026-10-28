#include "platform/audio_api.h"
#include "platform/audio_mixer_internal.h"
#include "platform/platform_api.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>

namespace audio {
namespace {

static_assert(std::atomic<uint32_t>::is_always_lock_free, "Audio needs lock-free 32-bit atomics");
using internal::WavInfo;
constexpr int kFade = internal::kFadeFrames;
constexpr size_t kReadBytes = 8192;

uint16_t le16(const uint8_t* p) { return uint16_t(p[0]) | (uint16_t(p[1]) << 8); }
uint32_t le32(const uint8_t* p) { return uint32_t(le16(p)) | (uint32_t(le16(p + 2)) << 16); }
float clamp(float v, float low = 0.0f, float high = 1.0f) {
    return std::isnan(v) ? low : std::max(low, std::min(v, high));
}
int16_t sample(const uint8_t* p) {
    const uint16_t v = le16(p);
    return static_cast<int16_t>(v < 32768 ? int32_t(v) : int32_t(v) - 65536);
}

template<class Read>
bool parse(size_t size, Read read, WavInfo* out) {
    uint8_t h[16];
    if (!out || size < 12 || !read(0, h, 12) || std::memcmp(h, "RIFF", 4) ||
        std::memcmp(h + 8, "WAVE", 4)) return false;
    const uint32_t riff_bytes = le32(h + 4);
    if (riff_bytes < 4 || size - 8 < riff_bytes) return false;
    const size_t end = size_t(riff_bytes) + 8;
    WavInfo info;
    bool fmt = false, data = false;
    for (size_t pos = 12; pos < end;) {
        if (end - pos < 8 || !read(pos, h, 8)) return false;
        const uint32_t bytes = le32(h + 4);
        pos += 8;
        if (bytes > end - pos) return false;
        const size_t padded = size_t(bytes) + (bytes & 1u);
        if (padded < bytes || padded > end - pos) return false;
        if (!std::memcmp(h, "fmt ", 4)) {
            if (fmt || bytes < 16 || !read(pos, h, 16) || le16(h) != 1 || le16(h + 14) != 16)
                return false;
            info.channels = le16(h + 2);
            info.rate = le32(h + 4);
            if ((info.channels != 1 && info.channels != 2) ||
                (info.rate != 11025 && info.rate != 22050 && info.rate != 44100) ||
                le16(h + 12) != info.channels * 2 ||
                le32(h + 8) != info.rate * info.channels * 2) return false;
            fmt = true;
        } else if (!std::memcmp(h, "data", 4)) {
            if (data || !bytes) return false;
            info.data_offset = pos;
            info.data_bytes = bytes;
            data = true;
        }
        pos += padded;
    }
    if (!fmt || !data || info.data_bytes % (info.channels * 2)) return false;
    *out = info;
    return true;
}

struct Ramp {
    float value = 0.0f, target = 0.0f, step = 0.0f;
    int left = 0;
    void to(float v) {
        target = v;
        step = (v - value) / float(kFade);
        left = kFade;
    }
    float next() {
        if (left && --left == 0) value = target;
        else if (left) value += step;
        return value;
    }
};
struct Sound {
    char name[32]{};
    const uint8_t* pcm = nullptr;
    uint32_t frames = 0, rate = 0, factor = 1;
    float phase_step = 1.0f;
    uint16_t channels = 0;
};
struct MainVoice {
    VoiceId id = 0;
    uint32_t age = 0;
    bool active = false, loop = false;
};
struct Tail {
    float l = 0.0f, r = 0.0f;
    int left = 0;
    float current_l() const { return l * (float(left) / float(kFade)); }
    float current_r() const { return r * (float(left) / float(kFade)); }
    void add(int32_t& lsum, int32_t& rsum) {
        if (!left) return;
        const float gain = float(--left) / float(kFade);
        lsum += int32_t(l * gain);
        rsum += int32_t(r * gain);
    }
};
struct Voice {
    VoiceId id = 0;
    const Sound* sound = nullptr;
    uint32_t frame = 0, phase = 0;
    float volume = 1.0f, pan = 0.0f, last_l = 0.0f, last_r = 0.0f;
    Ramp l, r;
    Tail tail;
    bool loop = false, stopping = false;
};
enum class Op { Play, Stop, Volume, Pan, Master, Sfx, MusicVolume, MusicStart, MusicStop };
struct Command {
    Op op = Op::Stop;
    VoiceId id = 0;
    SoundId sound = 0;
    float volume = 0.0f, pan = 0.0f;
    bool loop = false;
    WavInfo wav;
    uint32_t base = 0;
};
struct Music {
    WavInfo wav;
    uint32_t gen = 0, read = 0, phase = 0, factor = 1;
    float phase_step = 1.0f;
    bool active = false, starved = false;
    float last_l = 0.0f, last_r = 0.0f;
    Ramp fade;
    Tail tail;
};
struct Mixer {
    bool ready = false, device = false;
    uint8_t* arena = nullptr;
    uint8_t* ring = nullptr;
    uint8_t* scratch = nullptr;
    Sound* sounds = nullptr;
    size_t ring_size = 0;
    uint16_t sound_count = 0, sound_limit = 0;
    Stats stats;
    std::array<Command, internal::kCommandCapacity> commands{};
    std::atomic<uint32_t> head{0}, tail{0};
    std::array<MainVoice, kMaxVoices> main_voices{};
    std::array<Voice, kMaxVoices> voices{};
    std::array<std::atomic<uint32_t>, kMaxVoices> finished{};
    uint32_t age = 0, voice_gen = 0;
    Ramp master{1.0f, 1.0f}, sfx{1.0f, 1.0f}, music_volume{1.0f, 1.0f};
    Music music;
    int file = platform::kNoFile;
    WavInfo file_wav;
    size_t file_pos = 0;
    bool file_loop = false, file_eof = false, music_wanted = false;
    uint32_t music_gen = 0, write = 0;
    std::atomic<uint32_t> ring_read{0}, ring_write{0}, music_ack{0}, music_eof{0}, music_done{0}, music_ready{0};
    std::atomic<uint32_t> underruns{0}, mix_last{0}, mix_peak{0}, active{0};
};
Mixer g;

bool send(const Command& command) {
    if (!g.ready) return false;
    const uint32_t head = g.head.load(std::memory_order_relaxed);
    if (head - g.tail.load(std::memory_order_acquire) == internal::kCommandCapacity) {
        ++g.stats.command_overflows;
        return false;
    }
    g.commands[head % internal::kCommandCapacity] = command;
    g.head.store(head + 1, std::memory_order_release);
    return true;
}
void refresh() {
    for (int i = 0; i < kMaxVoices; ++i) {
        auto& v = g.main_voices[i];
        if (g.finished[i].load(std::memory_order_acquire) == v.id) v.active = false;
    }
    if (g.music_done.load(std::memory_order_acquire) == g.music_gen) g.music_wanted = false;
}
int voice_slot(VoiceId id) {
    const unsigned low = id & 15u;
    return low >= 1 && low <= kMaxVoices ? int(low - 1) : -1;
}
MainVoice* main_voice(VoiceId id) {
    refresh();
    const int i = voice_slot(id);
    return i >= 0 && g.main_voices[i].active && g.main_voices[i].id == id ? &g.main_voices[i] : nullptr;
}
void gains(Voice& v) {
    const float angle = (v.pan + 1.0f) * 0.7853981633974483f;
    v.l.to(v.pan == 1.0f ? 0.0f : v.volume * std::cos(angle));
    v.r.to(v.pan == -1.0f ? 0.0f : v.volume * std::sin(angle));
}
void consume_commands() {
    uint32_t tail = g.tail.load(std::memory_order_relaxed);
    const uint32_t head = g.head.load(std::memory_order_acquire);
    while (tail != head) {
        const Command c = g.commands[tail % internal::kCommandCapacity];
        ++tail;
        const int i = voice_slot(c.id);
        if (c.op == Op::Master) g.master.to(c.volume);
        else if (c.op == Op::Sfx) g.sfx.to(c.volume);
        else if (c.op == Op::MusicVolume) g.music_volume.to(c.volume);
        else if (c.op == Op::MusicStart || c.op == Op::MusicStop) {
            auto& m = g.music;
            const Tail tail_sample{m.last_l + m.tail.current_l(), m.last_r + m.tail.current_r(), kFade};
            m = {};
            m.tail = tail_sample;
            m.wav = c.wav;
            if (m.wav.rate) {
                m.factor = platform::kAudioRate / m.wav.rate;
                m.phase_step = float(m.wav.rate) / float(platform::kAudioRate);
            }
            m.gen = c.id;
            m.read = c.base;
            m.active = c.op == Op::MusicStart;
            g.ring_read.store(c.base, std::memory_order_release);
            g.music_ack.store(c.id, std::memory_order_release);
        } else if (i >= 0) {
            auto& v = g.voices[i];
            if (c.op == Op::Play) {
                const Tail old{v.last_l + v.tail.current_l(), v.last_r + v.tail.current_r(), kFade};
                v = {};
                v.tail = old;
                v.id = c.id;
                v.sound = &g.sounds[c.sound];
                v.volume = c.volume;
                v.pan = c.pan;
                v.loop = c.loop;
                gains(v);
            } else if (v.id == c.id && v.sound && !v.stopping) {
                if (c.op == Op::Stop) {
                    v.stopping = true;
                    v.l.to(0.0f);
                    v.r.to(0.0f);
                } else if (c.op == Op::Volume) {
                    v.volume = c.volume;
                    gains(v);
                } else if (c.op == Op::Pan) {
                    v.pan = c.pan;
                    gains(v);
                }
            }
        }
    }
    g.tail.store(tail, std::memory_order_release);
}
void mix_voice(Voice& v, int slot, int32_t& l, int32_t& r, float bus) {
    v.tail.add(l, r);
    if (!v.sound) return;
    const Sound& s = *v.sound;
    const uint32_t factor = s.factor;
    const uint32_t next = v.frame + 1 < s.frames ? v.frame + 1 : (v.loop ? 0 : v.frame);
    const float t = float(v.phase) * s.phase_step;
    const uint8_t* a = s.pcm + size_t(v.frame) * s.channels * 2;
    const uint8_t* b = s.pcm + size_t(next) * s.channels * 2;
    const int right = s.channels == 2 ? 2 : 0;
    const float sl = float(sample(a)) + float(sample(b) - sample(a)) * t;
    const float sr = float(sample(a + right)) + float(sample(b + right) - sample(a + right)) * t;
    float end_gain = 1.0f;
    if (!v.loop) {
        if (s.frames - v.frame <= uint32_t(kFade)) {
            const uint32_t remaining = (s.frames - v.frame) * factor - v.phase;
            end_gain = std::min(1.0f, float(remaining) / float(kFade));
        }
    }
    v.last_l = sl * v.l.next() * end_gain * bus;
    v.last_r = sr * v.r.next() * end_gain * bus;
    l += int32_t(v.last_l);
    r += int32_t(v.last_r);
    if (++v.phase == factor) { v.phase = 0; ++v.frame; }
    if (v.frame == s.frames && v.loop) v.frame = 0;
    if (v.frame == s.frames || (v.stopping && !v.l.left && !v.r.left)) {
        g.finished[slot].store(v.id, std::memory_order_release);
        v.sound = nullptr;
        v.last_l = v.last_r = 0.0f;
    }
}
int16_t ring_sample(uint32_t pos) {
    const size_t index = size_t(pos) & (g.ring_size - 1);
    return sample(g.ring + index);
}
void mix_music(int32_t& l, int32_t& r, float bus) {
    auto& m = g.music;
    m.tail.add(l, r);
    if (!m.active) return;
    // Первый update ещё не заполнил новый поток: это задержка старта, а не underrun.
    if (g.music_ready.load(std::memory_order_acquire) != m.gen) return;
    const uint32_t stride = m.wav.channels * 2;
    // EOF публикуется после последних данных; читать позицию записи нужно после EOF.
    const bool eof = g.music_eof.load(std::memory_order_acquire) == m.gen;
    const uint32_t available = g.ring_write.load(std::memory_order_acquire) - m.read;
    if (available < stride || (available < stride * 2 && !eof && m.phase != 0)) {
        if (!m.starved) {
            m.starved = true;
            m.tail = {m.last_l, m.last_r, kFade};
            m.tail.add(l, r);
            m.fade = {};
            if (!eof) g.underruns.fetch_add(1, std::memory_order_relaxed);
        }
        m.last_l = m.last_r = 0.0f;
        if (eof && !m.tail.left) {
            m.active = false;
            g.music_done.store(m.gen, std::memory_order_release);
        }
        return;
    }
    if (m.starved || (!m.fade.left && m.fade.target == 0.0f)) {
        m.starved = false;
        m.fade.to(1.0f);
    }
    const uint32_t factor = m.factor;
    const uint32_t next = available >= stride * 2 ? m.read + stride : m.read;
    const uint32_t right = stride == 4 ? 2 : 0;
    const float t = float(m.phase) * m.phase_step;
    const float sl = float(ring_sample(m.read)) + float(ring_sample(next) - ring_sample(m.read)) * t;
    const float sr = float(ring_sample(m.read + right)) +
        float(ring_sample(next + right) - ring_sample(m.read + right)) * t;
    const float gain = m.fade.next() * bus;
    m.last_l = sl * gain;
    m.last_r = sr * gain;
    l += int32_t(m.last_l);
    r += int32_t(m.last_r);
    if (++m.phase == factor) { m.phase = 0; m.read += stride; }
}
void close_music() {
    platform::file_close(g.file);
    g.file = platform::kNoFile;
}
void log_error(const char* message) { std::fprintf(stderr, "audio: %s\n", message); }

}  // namespace

namespace internal {

bool parse_wav(const void* data, size_t size, WavInfo* out) {
    if (!data) return false;
    return parse(size, [data, size](size_t offset, void* dst, size_t bytes) {
        if (offset > size || bytes > size - offset) return false;
        std::memcpy(dst, static_cast<const uint8_t*>(data) + offset, bytes);
        return true;
    }, out);
}

void mix(int16_t* dst, int frames, void*) {
    if (!dst || frames <= 0) return;
    if (!g.ready) { std::memset(dst, 0, size_t(frames) * 4); return; }
    const uint64_t start = platform::time_us();
    consume_commands();
    for (int f = 0; f < frames; ++f) {
        int32_t l = 0, r = 0;
        const float master = g.master.next();
        const float sfx = g.sfx.next() * master;
        const float music = g.music_volume.next() * master;
        for (int i = 0; i < kMaxVoices; ++i) mix_voice(g.voices[i], i, l, r, sfx);
        mix_music(l, r, music);
        dst[f * 2] = int16_t(std::max<int32_t>(-32768, std::min<int32_t>(l, 32767)));
        dst[f * 2 + 1] = int16_t(std::max<int32_t>(-32768, std::min<int32_t>(r, 32767)));
    }
    g.ring_read.store(g.music.read, std::memory_order_release);
    uint32_t active = 0;
    for (const auto& v : g.voices) if (v.sound) ++active;
    g.active.store(active, std::memory_order_relaxed);
    const uint32_t elapsed = uint32_t(platform::time_us() - start);
    g.mix_last.store(elapsed, std::memory_order_relaxed);
    if (elapsed > g.mix_peak.load(std::memory_order_relaxed)) g.mix_peak.store(elapsed, std::memory_order_relaxed);
}

}  // namespace internal

bool init(const Config& config) {
    if (g.ready || !config.sound_arena_bytes || !config.max_sounds ||
        config.music_ring_bytes < 8 || config.music_ring_bytes > 0x40000000u) return false;
    // Степень двойки сохраняет адресацию кольца при переполнении 32-битного счётчика.
    size_t ring_size = 8;
    while (ring_size <= config.music_ring_bytes / 2) ring_size *= 2;
    g.arena = static_cast<uint8_t*>(std::malloc(config.sound_arena_bytes));
    g.ring = static_cast<uint8_t*>(std::malloc(ring_size));
    g.scratch = static_cast<uint8_t*>(std::malloc(kReadBytes));
    g.sounds = new (std::nothrow) Sound[config.max_sounds];
    if (!g.arena || !g.ring || !g.scratch || !g.sounds) { shutdown(); return false; }
    g.ring_size = ring_size;
    g.sound_limit = config.max_sounds;
    g.stats.sound_arena_size = config.sound_arena_bytes;
    // Инициализация таймера PC не должна впервые сработать внутри колбэка.
    (void)platform::time_us();
    g.ready = true;
    g.device = platform::audio_start(internal::mix, nullptr);
    return true;
}

void shutdown() {
    platform::audio_stop();
    close_music();
    std::free(g.arena);
    std::free(g.ring);
    std::free(g.scratch);
    delete[] g.sounds;
    // Атомики не копируются; устройство уже остановлено, объект можно пересоздать.
    g.~Mixer();
    new (&g) Mixer{};
}

void update() {
    if (!g.ready) return;
    // Без устройства очередь всё равно обслуживается; дескрипторы не зависают навсегда.
    if (!g.device) {
        consume_commands();
    }
    refresh();
    if (g.file != platform::kNoFile && g.music_wanted &&
        g.music_ack.load(std::memory_order_acquire) == g.music_gen && !g.file_eof) {
        const size_t stride = g.file_wav.channels * 2;
        const uint32_t read = g.ring_read.load(std::memory_order_acquire);
        size_t free = g.ring_size - uint32_t(g.write - read);
        // Ограничение работы кадра размером кольца, в том числе для короткой loop-петли.
        while (free >= stride) {
            if (g.file_pos == g.file_wav.data_bytes) {
                if (g.file_loop) g.file_pos = 0;
                else { g.file_eof = true; break; }
            }
            size_t bytes = std::min({free, kReadBytes, g.file_wav.data_bytes - g.file_pos});
            bytes -= bytes % stride;
            size_t got = 0;
            if (!platform::file_read_at(g.file, g.file_wav.data_offset + g.file_pos, g.scratch, bytes, &got) ||
                got != bytes) {
                g.file_eof = true;
                break;
            }
            const size_t index = size_t(g.write) & (g.ring_size - 1);
            const size_t first = std::min(bytes, g.ring_size - index);
            std::memcpy(g.ring + index, g.scratch, first);
            std::memcpy(g.ring, g.scratch + first, bytes - first);
            g.write += uint32_t(bytes);
            g.file_pos += bytes;
            free -= bytes;
            g.ring_write.store(g.write, std::memory_order_release);
        }
        if (!g.file_loop && g.file_pos == g.file_wav.data_bytes) g.file_eof = true;
        if (g.file_eof) g.music_eof.store(g.music_gen, std::memory_order_release);
        g.music_ready.store(g.music_gen, std::memory_order_release);
    }
    g.stats.voices_active = uint8_t(g.active.load(std::memory_order_relaxed));
    g.stats.music_underruns = g.underruns.load(std::memory_order_relaxed);
    g.stats.mix_us_last = g.mix_last.load(std::memory_order_relaxed);
    g.stats.mix_us_peak = g.mix_peak.load(std::memory_order_relaxed);
}

SoundId sound_find(const char* name) {
    if (name) for (SoundId i = 0; i < g.sound_count; ++i)
        if (!std::strcmp(g.sounds[i].name, name)) return i;
    return kNoSound;
}

SoundId sound_load_wav(const char* name, const void* data, size_t size) {
    size_t len = 0;
    if (name) while (len < 32 && name[len]) {
        if (static_cast<unsigned char>(name[len]) > 127) break;
        ++len;
    }
    WavInfo wav;
    if (!g.ready || !name || !len || len == 32 || name[len] || sound_find(name) != kNoSound ||
        g.sound_count == g.sound_limit || !internal::parse_wav(data, size, &wav) ||
        wav.data_bytes > g.stats.sound_arena_size - g.stats.sound_arena_used) {
        log_error("invalid WAV/name or sound capacity exhausted");
        return kNoSound;
    }
    auto& s = g.sounds[g.sound_count];
    std::memcpy(s.name, name, len + 1);
    s.pcm = g.arena + g.stats.sound_arena_used;
    std::memcpy(g.arena + g.stats.sound_arena_used, static_cast<const uint8_t*>(data) + wav.data_offset,
                wav.data_bytes);
    s.channels = wav.channels;
    s.rate = wav.rate;
    s.factor = platform::kAudioRate / wav.rate;
    s.phase_step = float(wav.rate) / float(platform::kAudioRate);
    s.frames = uint32_t(wav.data_bytes / (wav.channels * 2));
    g.stats.sound_arena_used += wav.data_bytes;
    return g.sound_count++;
}

SoundId sound_load_file(const char* path) {
    if (!g.ready || !path) return kNoSound;
    const char* base = std::strrchr(path, '/');
    base = base ? base + 1 : path;
    const char* dot = std::strrchr(base, '.');
    const size_t len = dot ? size_t(dot - base) : std::strlen(base);
    char name[32];
    size_t size = 0;
    if (!len || len >= sizeof(name) || !platform::file_size(path, &size)) {
        log_error("invalid sound path"); return kNoSound;
    }
    std::memcpy(name, base, len); name[len] = 0;
    void* data = std::malloc(size);
    size_t got = 0;
    SoundId id = kNoSound;
    if (data && platform::file_read(path, data, size, &got) && got == size)
        id = sound_load_wav(name, data, size);
    else log_error("sound read failed");
    std::free(data);
    return id;
}

uint32_t sound_length_ms(SoundId id) {
    return id < g.sound_count ? uint32_t(uint64_t(g.sounds[id].frames) * 1000 / g.sounds[id].rate) : 0;
}

VoiceId play(SoundId sound, float volume, float pan, bool loop) {
    if (!g.ready || sound >= g.sound_count) return kNoVoice;
    refresh();
    int slot = -1;
    for (int i = 0; i < kMaxVoices; ++i) if (!g.main_voices[i].active) { slot = i; break; }
    if (slot < 0) for (int i = 0; i < kMaxVoices; ++i) {
        const auto& v = g.main_voices[i];
        if (!v.loop && (slot < 0 || uint32_t(g.age - v.age) > uint32_t(g.age - g.main_voices[slot].age))) slot = i;
    }
    if (slot < 0) return kNoVoice;
    g.voice_gen = (g.voice_gen + 1) & 0x0FFFFFFFu;
    if (!g.voice_gen) ++g.voice_gen;
    const VoiceId id = (g.voice_gen << 4) | uint32_t(slot + 1);
    Command c;
    c.op = Op::Play; c.id = id; c.sound = sound;
    c.volume = clamp(volume); c.pan = clamp(pan, -1.0f, 1.0f); c.loop = loop;
    if (!send(c)) return kNoVoice;
    g.main_voices[slot] = {id, ++g.age, true, loop};
    return id;
}
void stop(VoiceId id) {
    if (auto* v = main_voice(id)) {
        Command c; c.op = Op::Stop; c.id = id;
        if (send(c)) v->active = false;
    }
}
void set_volume(VoiceId id, float volume) {
    if (!main_voice(id)) return;
    Command c; c.op = Op::Volume; c.id = id; c.volume = clamp(volume); send(c);
}
void set_pan(VoiceId id, float pan) {
    if (!main_voice(id)) return;
    Command c; c.op = Op::Pan; c.id = id; c.pan = clamp(pan, -1.0f, 1.0f); send(c);
}
bool is_playing(VoiceId id) { return main_voice(id) != nullptr; }
void stop_all() { for (auto& v : g.main_voices) if (v.active) stop(v.id); }

bool music_play(const char* path, bool loop) {
    if (!g.ready) return false;
    size_t size = 0;
    const int file = platform::file_open(path);
    WavInfo wav;
    if (file == platform::kNoFile || !platform::file_size(path, &size) ||
        !parse(size, [file](size_t offset, void* dst, size_t bytes) {
            size_t got = 0;
            return platform::file_read_at(file, offset, dst, bytes, &got) && got == bytes;
        }, &wav)) {
        platform::file_close(file);
        music_stop();
        log_error("invalid music WAV/path");
        return false;
    }
    uint32_t gen = g.music_gen + 1;
    if (!gen) ++gen;
    Command c; c.op = Op::MusicStart; c.id = gen; c.wav = wav; c.base = g.write;
    if (!send(c)) { platform::file_close(file); return false; }
    close_music();
    g.file = file; g.file_wav = wav; g.file_pos = 0;
    g.file_loop = loop; g.file_eof = false; g.music_wanted = true; g.music_gen = gen;
    return true;
}
void music_stop() {
    if (!g.ready) return;
    uint32_t gen = g.music_gen + 1;
    if (!gen) ++gen;
    Command c; c.op = Op::MusicStop; c.id = gen; c.base = g.write;
    if (!send(c)) return;
    g.music_gen = gen;
    g.music_wanted = false;
    close_music();
}
bool music_playing() { refresh(); return g.music_wanted; }
void set_master_volume(float volume) { Command c; c.op = Op::Master; c.volume = clamp(volume); send(c); }
void set_sfx_volume(float volume) { Command c; c.op = Op::Sfx; c.volume = clamp(volume); send(c); }
void set_music_volume(float volume) { Command c; c.op = Op::MusicVolume; c.volume = clamp(volume); send(c); }
const Stats& stats() { return g.stats; }

}  // namespace audio
