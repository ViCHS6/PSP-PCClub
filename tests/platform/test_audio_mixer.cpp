#include <doctest/doctest.h>
#include <SDL.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

#include "platform/audio_api.h"
#include "platform/audio_mixer_internal.h"
#include "platform/platform_api.h"

namespace {
using Bytes = std::vector<uint8_t>;
void u16(Bytes& b, uint16_t v) { b.push_back(uint8_t(v)); b.push_back(uint8_t(v >> 8)); }
void u32(Bytes& b, uint32_t v) { u16(b, uint16_t(v)); u16(b, uint16_t(v >> 16)); }
void tag(Bytes& b, const char* s) { b.insert(b.end(), s, s + 4); }
void patch32(Bytes& b, size_t pos, uint32_t v) {
    for (int i = 0; i < 4; ++i) b[pos + i] = uint8_t(v >> (i * 8));
}
Bytes wav(const std::vector<int16_t>& pcm, uint32_t rate = 44100, uint16_t channels = 1, bool junk = false) {
    Bytes b;
    tag(b, "RIFF"); u32(b, 0); tag(b, "WAVE");
    if (junk) { tag(b, "JUNK"); u32(b, 3); b.insert(b.end(), {7, 8, 9, 0}); }
    tag(b, "fmt "); u32(b, 16); u16(b, 1); u16(b, channels); u32(b, rate);
    u32(b, rate * channels * 2); u16(b, channels * 2); u16(b, 16);
    tag(b, "data"); u32(b, uint32_t(pcm.size() * 2));
    for (int16_t s : pcm) u16(b, uint16_t(s));
    patch32(b, 4, uint32_t(b.size() - 8));
    return b;
}
struct AudioTest {
    explicit AudioTest(size_t ring = 128 * 1024, size_t arena = 1024 * 1024, uint16_t sounds = 64) {
        // Устройство не открывается даже при ручном запуске теста вне CTest.
        SDL_setenv("SDL_AUDIODRIVER", "pcclub_no_audio_device", 1);
        audio::Config c;
        c.music_ring_bytes = ring; c.sound_arena_bytes = arena; c.max_sounds = sounds;
        REQUIRE(audio::init(c));
    }
    ~AudioTest() { audio::shutdown(); std::remove("audio_fixture.wav"); }
    audio::SoundId load(const std::vector<int16_t>& pcm, uint32_t rate = 44100, uint16_t channels = 1,
                        const char* name = "test") {
        const auto b = wav(pcm, rate, channels);
        const auto id = audio::sound_load_wav(name, b.data(), b.size());
        REQUIRE(id != audio::kNoSound);
        return id;
    }
    void file(const Bytes& b) {
        FILE* f = std::fopen("audio_fixture.wav", "wb");
        REQUIRE(f != nullptr);
        const size_t wrote = std::fwrite(b.data(), 1, b.size(), f);
        std::fclose(f);
        REQUIRE(wrote == b.size());
    }
    std::vector<int16_t> mix(int frames) {
        std::vector<int16_t> out(size_t(frames) * 2, 123);
        audio::internal::mix(out.data(), frames);
        return out;
    }
};
}  // namespace

TEST_CASE("WAV accepts all PCM16 rates channels and odd unknown chunks") {
    for (uint32_t rate : {11025u, 22050u, 44100u}) for (uint16_t channels : {1, 2}) {
        const auto b = wav(std::vector<int16_t>(12, -123), rate, channels, true);
        audio::internal::WavInfo info;
        REQUIRE(audio::internal::parse_wav(b.data(), b.size(), &info));
        CHECK(info.rate == rate); CHECK(info.channels == channels);
        CHECK(info.data_bytes == 24); CHECK(info.data_offset == 56);
    }
}

TEST_CASE("WAV rejects truncated overflowing unsupported and inconsistent headers") {
    const auto valid = wav(std::vector<int16_t>(8, 100));
    audio::internal::WavInfo info;
    CHECK_FALSE(audio::internal::parse_wav(nullptr, 100, &info));
    CHECK_FALSE(audio::internal::parse_wav(valid.data(), valid.size(), nullptr));
    for (size_t size = 0; size < valid.size(); ++size)
        CHECK_FALSE(audio::internal::parse_wav(valid.data(), size, &info));
    for (size_t pos : {size_t(0), size_t(8), size_t(12), size_t(36)}) {
        auto b = valid; b[pos] = '!';
        CHECK_FALSE(audio::internal::parse_wav(b.data(), b.size(), &info));
    }
    for (uint8_t encoding : {0, 3, 17}) {
        auto b = valid; b[20] = encoding;
        CHECK_FALSE(audio::internal::parse_wav(b.data(), b.size(), &info));
    }
    for (size_t pos : {size_t(22), size_t(24), size_t(28), size_t(32), size_t(34)}) {
        auto b = valid; b[pos] = 8;
        CHECK_FALSE(audio::internal::parse_wav(b.data(), b.size(), &info));
    }
    for (size_t pos : {size_t(4), size_t(16), size_t(40)}) {
        auto b = valid; patch32(b, pos, 0xFFFFFFFFu);
        CHECK_FALSE(audio::internal::parse_wav(b.data(), b.size(), &info));
    }
    auto b = valid; patch32(b, 40, 15);
    CHECK_FALSE(audio::internal::parse_wav(b.data(), b.size(), &info));
    b = wav({100, 200}, 44100, 2); patch32(b, 40, 2);
    CHECK_FALSE(audio::internal::parse_wav(b.data(), b.size(), &info));
    b = wav({100}, 44100, 1, true); b.erase(b.begin() + 23); patch32(b, 4, uint32_t(b.size() - 8));
    CHECK_FALSE(audio::internal::parse_wav(b.data(), b.size(), &info));
}

TEST_CASE("WAV can place data before fmt and extend fmt") {
    const auto valid = wav({-32768, 32767});
    Bytes b(valid.begin(), valid.begin() + 12);
    b.insert(b.end(), valid.begin() + 36, valid.end());
    b.insert(b.end(), valid.begin() + 12, valid.begin() + 36);
    audio::internal::WavInfo info;
    CHECK(audio::internal::parse_wav(b.data(), b.size(), &info));
    b = valid; b.insert(b.begin() + 36, {0, 0}); patch32(b, 16, 18); patch32(b, 4, uint32_t(b.size() - 8));
    CHECK(audio::internal::parse_wav(b.data(), b.size(), &info));
}

TEST_CASE("audio init validation reinit missing device and sound registry limits") {
    audio::Config config; config.music_ring_bytes = 0;
    CHECK_FALSE(audio::init(config));
    AudioTest test(1000, 4000, 1);
    CHECK_FALSE(audio::init(audio::Config{}));
    const auto id = test.load(std::vector<int16_t>(1000, 100));
    CHECK(audio::sound_find("test") == id);
    CHECK(audio::sound_find(nullptr) == audio::kNoSound);
    CHECK(audio::sound_length_ms(id) == 22);
    CHECK(audio::sound_length_ms(audio::kNoSound) == 0);
    const auto b = wav({1, 2});
    CHECK(audio::sound_load_wav("test", b.data(), b.size()) == audio::kNoSound);
    CHECK(audio::sound_load_wav("another", b.data(), b.size()) == audio::kNoSound);
    CHECK(audio::play(audio::kNoSound) == audio::kNoVoice);
    CHECK(audio::stats().sound_arena_used == 2000);
}

TEST_CASE("sound loading rejects names bad wav arena exhaustion and loads basename") {
    AudioTest test(1024, 64);
    const auto b = wav({123, -456});
    for (const char* name : {"", "abcdefghijklmnopqrstuvwxyz012345", "bad\xFF"})
        CHECK(audio::sound_load_wav(name, b.data(), b.size()) == audio::kNoSound);
    CHECK(audio::sound_load_wav(nullptr, b.data(), b.size()) == audio::kNoSound);
    CHECK(audio::sound_load_wav("bad", nullptr, 100) == audio::kNoSound);
    const auto big = wav(std::vector<int16_t>(100, 0));
    CHECK(audio::sound_load_wav("big", big.data(), big.size()) == audio::kNoSound);
    CHECK(audio::stats().sound_arena_used == 0);
    test.file(b);
    CHECK(audio::sound_load_file("audio_fixture.wav") != audio::kNoSound);
    CHECK(audio::sound_find("audio_fixture") != audio::kNoSound);
    CHECK(audio::sound_load_file("missing.wav") == audio::kNoSound);
}

TEST_CASE("no voices produces exact silence") {
    AudioTest test;
    const auto out = test.mix(platform::kAudioFrames);
    CHECK(std::all_of(out.begin(), out.end(), [](int16_t v) { return v == 0; }));
}

TEST_CASE("one voice has exact PCM samples after attack and preserves stereo") {
    AudioTest test;
    std::vector<int16_t> pcm(2048);
    for (size_t i = 0; i < pcm.size() / 2; ++i) { pcm[i * 2] = int16_t(int(i) * 17 - 8000); pcm[i * 2 + 1] = -12345; }
    const auto s = test.load(pcm, 44100, 2);
    const auto v = audio::play(s, 1.0f, -1.0f, true);
    REQUIRE(audio::is_playing(v));
    const auto out = test.mix(700);
    CHECK(out[0] == int16_t(float(pcm[0]) / audio::internal::kFadeFrames));
    for (int i = 221; i < 700; ++i) { CHECK(out[i * 2] == pcm[i * 2]); CHECK(out[i * 2 + 1] == 0); }
    audio::set_pan(v, 1.0f);
    test.mix(221);
    const auto right = test.mix(50);
    for (int i = 0; i < 50; ++i) { CHECK(right[i * 2] == 0); CHECK(right[i * 2 + 1] == -12345); }
}

TEST_CASE("center pan uses equal power and clamps controls") {
    AudioTest test;
    const auto s = test.load(std::vector<int16_t>(1024, 10000));
    const auto v = audio::play(s, 2.0f, 0.0f, true);
    test.mix(221);
    const auto center = test.mix(1);
    CHECK(center[0] == 7071); CHECK(center[1] == 7071);
    audio::set_pan(v, -100.0f); test.mix(221);
    CHECK(test.mix(1)[0] == 10000);
    audio::set_volume(v, -1.0f); test.mix(221);
    CHECK(test.mix(1)[0] == 0);
}

TEST_CASE("eight loud voices saturate int16 instead of wrapping") {
    AudioTest test;
    const auto positive = test.load(std::vector<int16_t>(1000, 30000));
    const auto negative = test.load(std::vector<int16_t>(1000, -30000), 44100, 1, "negative");
    for (int i = 0; i < 8; ++i) REQUIRE(audio::play(positive, 1.0f, -1.0f, true) != audio::kNoVoice);
    test.mix(221); CHECK(test.mix(1)[0] == 32767);
    audio::stop_all(); test.mix(221);
    for (int i = 0; i < 8; ++i) REQUIRE(audio::play(negative, 1.0f, 1.0f, true) != audio::kNoVoice);
    test.mix(221); CHECK(test.mix(1)[1] == -32768);
}

TEST_CASE("linear resampling has exact duration and interpolated samples at half rate") {
    AudioTest test;
    std::vector<int16_t> pcm(500);
    for (int i = 0; i < 500; ++i) pcm[i] = int16_t(i * 20);
    const auto s = test.load(pcm, 22050);
    const auto v = audio::play(s, 1.0f, -1.0f);
    const auto out = test.mix(999);
    CHECK(audio::is_playing(v));
    for (int i = 221; i < 779; ++i) CHECK(out[i * 2] == i * 10);
    test.mix(1);
    CHECK_FALSE(audio::is_playing(v));
    CHECK(test.mix(1)[0] == 0);
}

TEST_CASE("quarter rate loops interpolate across the loop seam") {
    AudioTest test;
    const auto s = test.load({0, 1000}, 11025);
    audio::play(s, 1.0f, -1.0f, true);
    test.mix(224);
    const auto out = test.mix(8);
    const int16_t expected[] = {0, 250, 500, 750, 1000, 750, 500, 250};
    for (int i = 0; i < 8; ++i) CHECK(out[i * 2] == expected[i]);
}

TEST_CASE("ninth voice steals oldest nonloop and stale ids cannot change replacement") {
    AudioTest test;
    const auto s = test.load(std::vector<int16_t>(10000, 10000));
    std::array<audio::VoiceId, 8> voices;
    voices[0] = audio::play(s, 1.0f, -1.0f, true);
    for (int i = 1; i < 8; ++i) voices[i] = audio::play(s, 1.0f, -1.0f);
    test.mix(300);
    const auto replacement = audio::play(s, 1.0f, 1.0f);
    REQUIRE(replacement != audio::kNoVoice);
    CHECK_FALSE(audio::is_playing(voices[1]));
    CHECK(audio::is_playing(voices[0]));
    audio::stop(voices[1]); audio::set_volume(voices[1], 0.0f); audio::set_pan(voices[1], -1.0f);
    test.mix(300);
    CHECK(audio::is_playing(replacement)); CHECK(test.mix(1)[1] == 10000);
    audio::stop_all(); test.mix(221);
    for (int i = 0; i < 8; ++i) REQUIRE(audio::play(s, 1.0f, 0.0f, true) != audio::kNoVoice);
    CHECK(audio::play(s) == audio::kNoVoice);
    audio::stop(0xFFFFFFFFu); CHECK_FALSE(audio::is_playing(0xFFFFFFFFu));
}

TEST_CASE("stop fades over five milliseconds without a discontinuity") {
    AudioTest test;
    const auto s = test.load(std::vector<int16_t>(1000, 22000));
    const auto v = audio::play(s, 1.0f, -1.0f, true);
    test.mix(300); audio::stop(v);
    CHECK_FALSE(audio::is_playing(v));
    const auto out = test.mix(300);
    int previous = 22000;
    for (int i = 0; i < 300; ++i) {
        CHECK(std::abs(int(out[i * 2]) - previous) <= 101);
        previous = out[i * 2];
    }
    CHECK(previous == 0);
}

TEST_CASE("master sfx and music volumes ramp independently") {
    AudioTest test;
    const auto s = test.load(std::vector<int16_t>(1000, 20000));
    audio::play(s, 1.0f, -1.0f, true); test.mix(221);
    audio::set_sfx_volume(0.5f); test.mix(221); CHECK(test.mix(1)[0] == 10000);
    audio::set_master_volume(0.5f); test.mix(221); CHECK(test.mix(1)[0] == 5000);
    audio::set_music_volume(0.0f); test.mix(221); CHECK(test.mix(1)[0] == 5000);
}

TEST_CASE("stop during natural end fade never restores full gain") {
    AudioTest test;
    const auto s = test.load(std::vector<int16_t>(1000, 22000));
    const auto v = audio::play(s, 1.0f, -1.0f);
    const auto first = test.mix(900);
    audio::stop(v);
    const auto out = test.mix(300);
    int previous = first[first.size() - 2];
    for (int i = 0; i < 300; ++i) {
        CHECK(std::abs(int(out[i * 2]) - previous) <= 150);
        previous = out[i * 2];
    }
    CHECK(previous == 0);
}

TEST_CASE("command overflow is counted and rejected play keeps old state") {
    AudioTest test;
    const auto s = test.load({1000});
    for (uint32_t i = 0; i < audio::internal::kCommandCapacity; ++i) audio::set_sfx_volume(0.5f);
    CHECK(audio::play(s) == audio::kNoVoice);
    CHECK(audio::stats().command_overflows == 1);
    test.mix(1);
    CHECK(audio::play(s) != audio::kNoVoice);
}

TEST_CASE("PC streaming file reads EOF invalid paths descriptors and fixed pool") {
    AudioTest test;
    const auto b = wav({1, 2, 3}); test.file(b);
    CHECK(platform::file_open(nullptr) == platform::kNoFile);
    CHECK(platform::file_open("/invalid") == platform::kNoFile);
    CHECK(platform::file_open("ef0:/invalid") == platform::kNoFile);
    std::array<int, platform::kMaxOpenFiles> files;
    for (auto& f : files) { f = platform::file_open("audio_fixture.wav"); REQUIRE(f >= 0); }
    CHECK(platform::file_open("audio_fixture.wav") == platform::kNoFile);
    uint8_t dst[32]{}; size_t got = 999;
    CHECK(platform::file_read_at(files[0], b.size() - 3, dst, sizeof(dst), &got));
    CHECK(got == 3); CHECK(std::memcmp(dst, b.data() + b.size() - 3, 3) == 0);
    CHECK(platform::file_read_at(files[0], b.size() + 100, dst, sizeof(dst), &got)); CHECK(got == 0);
    CHECK(platform::file_read_at(files[0], 0, dst, 4, &got)); CHECK(std::memcmp(dst, "RIFF", 4) == 0);
    CHECK_FALSE(platform::file_read_at(-1, 0, dst, 1, &got));
    CHECK_FALSE(platform::file_read_at(1000, 0, dst, 1, &got));
    for (int f : files) platform::file_close(f);
    platform::file_close(files[0]); platform::file_close(platform::kNoFile);
    CHECK_FALSE(platform::file_read_at(files[0], 0, dst, 1, &got));
}

TEST_CASE("music underrun fades once per starvation episode and resumes") {
    AudioTest test(2048);
    test.file(wav(std::vector<int16_t>(4000, 22000), 22050));
    REQUIRE(audio::music_play("audio_fixture.wav", true));
    audio::update(); test.mix(500);
    const auto out = test.mix(2500);
    int previous = 22000;
    for (int i = 0; i < 2500; ++i) {
        CHECK(std::abs(int(out[i * 2]) - previous) <= 101);
        previous = out[i * 2];
    }
    audio::update(); CHECK(audio::stats().music_underruns == 1);
    CHECK(previous == 0);
    CHECK(test.mix(500)[998] == 22000);
    test.mix(3000); audio::update(); CHECK(audio::stats().music_underruns == 2);
}

TEST_CASE("music loop crosses data boundary and ring wrap without a gap") {
    AudioTest test(4096);
    std::vector<int16_t> pcm(127, 5000);
    test.file(wav(pcm, 22050, 1, true));
    REQUIRE(audio::music_play("audio_fixture.wav", true));
    audio::update(); test.mix(221);
    for (int block = 0; block < 40; ++block) {
        const auto out = test.mix(1024);
        CHECK(std::all_of(out.begin(), out.end(), [](int16_t v) { return v == 5000; }));
        audio::update();
    }
    CHECK(audio::stats().music_underruns == 0);
    CHECK(audio::music_playing());
}

TEST_CASE("music loop resamples between last and first source frames") {
    AudioTest test(4096);
    test.file(wav({0, 1000}, 22050));
    REQUIRE(audio::music_play("audio_fixture.wav", true));
    audio::update(); test.mix(224);
    const auto out = test.mix(4);
    const int16_t expected[] = {0, 500, 1000, 500};
    for (int i = 0; i < 4; ++i) CHECK(out[i * 2] == expected[i]);
}

TEST_CASE("music nonloop EOF fades finishes and invalid files fail safely") {
    AudioTest test;
    CHECK_FALSE(audio::music_play("missing.wav", true));
    test.file({1, 2, 3}); CHECK_FALSE(audio::music_play("audio_fixture.wav", true));
    test.file(wav(std::vector<int16_t>(400, 22000)));
    REQUIRE(audio::music_play("audio_fixture.wav", false));
    audio::update(); CHECK(audio::music_playing());
    const auto out = test.mix(900);
    CHECK_FALSE(audio::music_playing());
    int previous = out[600];
    for (int i = 301; i < 900; ++i) {
        CHECK(std::abs(int(out[i * 2]) - previous) <= 101);
        previous = out[i * 2];
    }
    audio::update(); CHECK(audio::stats().music_underruns == 0);
    CHECK(previous == 0);
}

TEST_CASE("music stop and replacement fade with pending stream commands") {
    AudioTest test(4096);
    test.file(wav(std::vector<int16_t>(1000, 22000)));
    REQUIRE(audio::music_play("audio_fixture.wav", true));
    audio::update(); test.mix(500);
    audio::music_stop(); CHECK_FALSE(audio::music_playing());
    const auto out = test.mix(300);
    int previous = 22000;
    for (int i = 0; i < 300; ++i) {
        CHECK(std::abs(int(out[i * 2]) - previous) <= 101);
        previous = out[i * 2];
    }
    REQUIRE(audio::music_play("audio_fixture.wav", true));
    REQUIRE(audio::music_play("audio_fixture.wav", true));
    audio::update(); test.mix(500); audio::update();
    CHECK(audio::music_playing()); CHECK(audio::stats().music_underruns == 0);
}

TEST_CASE("music deferred start and replacement keep the ring readable") {
    AudioTest test(4096);
    test.file(wav(std::vector<int16_t>(1000, 22000)));
    REQUIRE(audio::music_play("audio_fixture.wav", true));
    test.mix(1);
    audio::update(); test.mix(500);
    REQUIRE(audio::music_play("audio_fixture.wav", true));
    // Колбэк принимает смену потока до ближайшего update, как при работающем устройстве.
    const auto transition = test.mix(1);
    CHECK(std::abs(int(transition[0]) - 22000) <= 101);
    audio::update();
    const auto out = test.mix(500);
    CHECK(out[998] == 22000);
    audio::update(); CHECK(audio::stats().music_underruns == 0);
}

TEST_CASE("PC benchmark eight voices plus streaming music") {
    AudioTest test;
    const auto b = wav(std::vector<int16_t>(2000, 1000), 22050);
    test.file(b);
    const auto s = audio::sound_load_wav("bench", b.data(), b.size());
    REQUIRE(s != audio::kNoSound);
    for (int i = 0; i < 8; ++i) audio::play(s, 0.05f, float(i) / 7.0f * 2.0f - 1.0f, true);
    REQUIRE(audio::music_play("audio_fixture.wav", true));
    audio::update();
    std::array<int16_t, platform::kAudioFrames * 2> dst{};
    uint64_t sum = 0;
    uint32_t peak = 0;
    constexpr int calls = 2000;
    for (int i = 0; i < calls; ++i) {
        audio::internal::mix(dst.data(), platform::kAudioFrames);
        audio::update();
        sum += audio::stats().mix_us_last;
        peak = std::max(peak, audio::stats().mix_us_last);
    }
    CHECK(audio::stats().voices_active == 8);
    CHECK(audio::stats().music_underruns == 0);
    std::printf("Audio benchmark: 8 voices + music, %d frames, %d calls: mean %llu us, peak %u us\n",
                platform::kAudioFrames, calls, static_cast<unsigned long long>(sum / calls), static_cast<unsigned>(peak));
}
