#include "ants_app/audio_mixer.hpp"

#include <cmath>
#include <cstring>
#include <algorithm>
#include <iostream>

#if defined(__has_include)
  #if __has_include(<SDL.h>)
    #include <SDL.h>
  #elif __has_include(<SDL2/SDL.h>)
    #include <SDL2/SDL.h>
  #endif
#else
  #include <SDL2/SDL.h>
#endif

#include <fstream>

// dr_mp3 is third-party code: its implementation is compiled without the warnings that the game's own code is held to. The pragmas are GCC / Clang
// only (MSVC warns C4068 about a pragma it does not know; its /W4 set is configured in CMakeLists.txt).
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wsign-conversion"
#pragma GCC diagnostic ignored "-Wconversion"
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#endif
#define DR_MP3_IMPLEMENTATION
#include "ants_app/dr_mp3.h"
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic pop
#endif

namespace ants::app {

namespace {

constexpr size_t kMixChunkFrames = 512;
constexpr size_t kMusicSrcBufFrames = 64;
constexpr float kPi = 3.14159265358979323846f;
constexpr float kTwoPi = 6.28318530717958647692f;

inline float hermite4(float y0, float y1, float y2, float y3, float t) noexcept {
    const float c0 = y1;
    const float c1 = 0.5f * (y2 - y0);
    const float c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
    const float c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
    return ((c3 * t + c2) * t + c1) * t + c0;
}

inline void configure_channel_lpf(uint32_t clip_rate, uint32_t output_rate, bool& out_enabled, float& out_alpha) noexcept {
    if (clip_rate == 0 || output_rate == 0 || clip_rate >= output_rate) {
        out_enabled = false;
        out_alpha = 1.0f;
        return;
    }
    float fc = 0.0f;
    if (clip_rate == 11025u) {
        fc = 5000.0f;
    } else if (clip_rate == 22050u) {
        fc = 10000.0f;
    } else {
        fc = std::min(static_cast<float>(clip_rate) * (5000.0f / 11025.0f),
                      static_cast<float>(output_rate) * 0.45f);
    }
    out_enabled = true;
    out_alpha = std::clamp(1.0f - std::exp(-kTwoPi * fc / static_cast<float>(output_rate)), 0.0f, 1.0f);
}

inline void sample_clip_hermite_lpf(const DecodedClip& clip,
                                    double cursor,
                                    bool loop,
                                    bool lpf_enabled,
                                    float lpf_alpha,
                                    float& lpf_l,
                                    float& lpf_r,
                                    float& out_l,
                                    float& out_r) noexcept {
    const size_t n = clip.frame_count();
    const uint16_t chs = clip.channels;
    const size_t i0 = std::min(static_cast<size_t>(cursor), n - 1u);
    const float t = static_cast<float>(cursor - static_cast<double>(i0));

    size_t im1 = 0;
    size_t i1 = 0;
    size_t i2 = 0;
    if (loop) {
        im1 = (i0 == 0) ? (n - 1u) : (i0 - 1u);
        i1 = (i0 + 1u < n) ? (i0 + 1u) : 0u;
        i2 = (i0 + 2u < n) ? (i0 + 2u) : ((i0 + 2u) % n);
    } else {
        im1 = (i0 > 0) ? (i0 - 1u) : 0u;
        i1 = (i0 + 1u < n) ? (i0 + 1u) : (n - 1u);
        i2 = (i0 + 2u < n) ? (i0 + 2u) : (n - 1u);
    }

    const float* data = clip.samples.data();
    if (chs == 2) {
        float sl = hermite4(data[im1 * 2u], data[i0 * 2u], data[i1 * 2u], data[i2 * 2u], t);
        float sr = hermite4(data[im1 * 2u + 1u], data[i0 * 2u + 1u], data[i1 * 2u + 1u], data[i2 * 2u + 1u], t);
        if (lpf_enabled) {
            lpf_l += lpf_alpha * (sl - lpf_l);
            lpf_r += lpf_alpha * (sr - lpf_r);
            sl = lpf_l;
            sr = lpf_r;
        }
        out_l = sl;
        out_r = sr;
    } else {
        float s = hermite4(data[im1], data[i0], data[i1], data[i2], t);
        if (lpf_enabled) {
            lpf_l += lpf_alpha * (s - lpf_l);
            s = lpf_l;
        }
        out_l = s;
        out_r = s;
    }
}

} // anonymous namespace

struct AudioMixer::MusicStream {
    std::vector<uint8_t> file_data;
    drmp3 decoder{};
    bool is_open{false};
    bool is_playing{false};
    bool is_paused{false};
    bool stopping{false};
    bool loop{true};
    float volume{0.8f};
    float fade_volume{1.0f};
    float gain_now{0.0f};
    float fade_duration{0.0f};
    float fade_timer{0.0f};
    bool is_fading{false};
    std::string filepath;

    // Fixed-size sample-rate conversion state (used when decoder.sampleRate != output_sample_rate_; zero heap allocation)
    double src_phase{0.0};
    float src_prev_l{0.0f};
    float src_prev_r{0.0f};
    float src_curr_l{0.0f};
    float src_curr_r{0.0f};
    bool src_primed{false};
    std::array<float, kMusicSrcBufFrames * 2> src_buf{};
    size_t src_buf_pos{0};
    size_t src_buf_len{0};

    void reset_src() noexcept {
        src_phase = 0.0;
        src_prev_l = 0.0f;
        src_prev_r = 0.0f;
        src_curr_l = 0.0f;
        src_curr_r = 0.0f;
        src_primed = false;
        src_buf_pos = 0;
        src_buf_len = 0;
    }

    void cleanup() {
        if (is_open) {
            drmp3_uninit(&decoder);
            is_open = false;
        }
        file_data.clear();
        is_playing = false;
        is_paused = false;
        stopping = false;
        is_fading = false;
        fade_volume = 1.0f;
        gain_now = 0.0f;
        reset_src();
    }

    bool pull_source_frame(float& out_l, float& out_r) noexcept {
        const uint32_t mp3_channels = decoder.channels;
        if (mp3_channels != 1 && mp3_channels != 2) return false;

        if (src_buf_pos >= src_buf_len) {
            src_buf_pos = 0;
            src_buf_len = 0;

            drmp3_uint64 read = drmp3_read_pcm_frames_f32(&decoder, kMusicSrcBufFrames, src_buf.data());
            if (read == 0) {
                if (loop && !stopping) {
                    drmp3_seek_to_pcm_frame(&decoder, 0);
                    read = drmp3_read_pcm_frames_f32(&decoder, kMusicSrcBufFrames, src_buf.data());
                    if (read == 0) {
                        return false;
                    }
                } else {
                    is_playing = false;
                    stopping = false;
                    return false;
                }
            }
            src_buf_len = static_cast<size_t>(read);
        }

        const size_t idx = src_buf_pos++;
        if (mp3_channels == 2) {
            out_l = src_buf[idx * 2u];
            out_r = src_buf[idx * 2u + 1u];
        } else {
            out_l = src_buf[idx];
            out_r = src_buf[idx];
        }
        return true;
    }

    void read_stereo_chunk(float* dst_stereo, size_t chunk_frames, uint32_t output_sample_rate) noexcept {
        const uint32_t mp3_channels = decoder.channels;
        if (mp3_channels != 1 && mp3_channels != 2) {
            std::memset(dst_stereo, 0, chunk_frames * 2u * sizeof(float));
            return;
        }

        const uint32_t mp3_rate = decoder.sampleRate;
        if (mp3_rate == 0 || mp3_rate == output_sample_rate) {
            // Fast 1:1 path (zero resampling overhead)
            float mono_buf[kMixChunkFrames];
            float* read_dst = (mp3_channels == 2) ? dst_stereo : mono_buf;

            size_t total_read = 0;
            while (total_read < chunk_frames) {
                const drmp3_uint64 need = static_cast<drmp3_uint64>(chunk_frames - total_read);
                drmp3_uint64 read = drmp3_read_pcm_frames_f32(&decoder, need, read_dst + total_read * mp3_channels);
                if (read == 0) {
                    if (loop && !stopping) {
                        drmp3_seek_to_pcm_frame(&decoder, 0);
                        read = drmp3_read_pcm_frames_f32(&decoder, need, read_dst + total_read * mp3_channels);
                        if (read == 0) break;
                    } else {
                        is_playing = false;
                        stopping = false;
                        break;
                    }
                }
                total_read += static_cast<size_t>(read);
            }

            if (mp3_channels == 2) {
                if (total_read < chunk_frames) {
                    std::memset(dst_stereo + total_read * 2u, 0, (chunk_frames - total_read) * 2u * sizeof(float));
                }
            } else {
                for (size_t i = 0; i < total_read; ++i) {
                    dst_stereo[i * 2u] = mono_buf[i];
                    dst_stereo[i * 2u + 1u] = mono_buf[i];
                }
                if (total_read < chunk_frames) {
                    std::memset(dst_stereo + total_read * 2u, 0, (chunk_frames - total_read) * 2u * sizeof(float));
                }
            }
            return;
        }

        // Fallback linear sample-rate conversion when MP3 rate != output_sample_rate
        const double step = static_cast<double>(mp3_rate) / static_cast<double>(output_sample_rate);
        if (!src_primed) {
            if (!pull_source_frame(src_prev_l, src_prev_r)) {
                std::memset(dst_stereo, 0, chunk_frames * 2u * sizeof(float));
                return;
            }
            if (!pull_source_frame(src_curr_l, src_curr_r)) {
                src_curr_l = src_prev_l;
                src_curr_r = src_prev_r;
            }
            src_phase = 0.0;
            src_primed = true;
        }

        for (size_t f = 0; f < chunk_frames; ++f) {
            const float frac = static_cast<float>(src_phase);
            dst_stereo[f * 2u] = src_prev_l + frac * (src_curr_l - src_prev_l);
            dst_stereo[f * 2u + 1u] = src_prev_r + frac * (src_curr_r - src_prev_r);

            src_phase += step;
            while (src_phase >= 1.0) {
                src_prev_l = src_curr_l;
                src_prev_r = src_curr_r;
                if (!pull_source_frame(src_curr_l, src_curr_r)) {
                    src_curr_l = 0.0f;
                    src_curr_r = 0.0f;
                }
                src_phase -= 1.0;
            }
        }
    }

    ~MusicStream() {
        cleanup();
    }
};

static void sdl_audio_callback(void* userdata, uint8_t* stream, int len) {
    auto* mixer = static_cast<AudioMixer*>(userdata);
    if (!mixer || len <= 0) return;
    size_t num_frames = static_cast<size_t>(len) / (2 * sizeof(int16_t));
    mixer->mix_samples_i16(reinterpret_cast<int16_t*>(stream), num_frames);
}

AudioMixer::AudioMixer() : music_stream_(std::make_unique<MusicStream>()) {}

AudioMixer::~AudioMixer() {
    shutdown_sdl_audio();
    music_stream_.reset();
}

AudioMixer::AudioMixer(AudioMixer&& other) noexcept {
    std::lock_guard<std::mutex> lock(other.mixer_mutex_);
    archive_ = other.archive_;
    output_sample_rate_ = other.output_sample_rate_;
    base_clips_ = std::move(other.base_clips_);
    override_clips_ = std::move(other.override_clips_);
    channels_ = std::move(other.channels_);
    release_tails_ = std::move(other.release_tails_);
    start_log_ = other.start_log_;
    listener_x_ = other.listener_x_;
    listener_y_ = other.listener_y_;
    output_gain_target_ = other.output_gain_target_;
    output_gain_now_ = other.output_gain_now_;
    sound_volume_ = other.sound_volume_;
    headless_mode_.store(other.headless_mode_.load(std::memory_order_relaxed), std::memory_order_relaxed);
    sdl_audio_device_ = other.sdl_audio_device_;
    music_stream_ = std::move(other.music_stream_);

    other.archive_ = nullptr;
    other.start_log_ = nullptr;
    other.sdl_audio_device_ = 0;
}

AudioMixer& AudioMixer::operator=(AudioMixer&& other) noexcept {
    if (this != &other) {
        shutdown_sdl_audio();
        std::scoped_lock lock(mixer_mutex_, other.mixer_mutex_);
        archive_ = other.archive_;
        output_sample_rate_ = other.output_sample_rate_;
        base_clips_ = std::move(other.base_clips_);
        override_clips_ = std::move(other.override_clips_);
        channels_ = std::move(other.channels_);
        release_tails_ = std::move(other.release_tails_);
        start_log_ = other.start_log_;
        listener_x_ = other.listener_x_;
        listener_y_ = other.listener_y_;
        output_gain_target_ = other.output_gain_target_;
        output_gain_now_ = other.output_gain_now_;
        sound_volume_ = other.sound_volume_;
        headless_mode_.store(other.headless_mode_.load(std::memory_order_relaxed), std::memory_order_relaxed);
        sdl_audio_device_ = other.sdl_audio_device_;
        music_stream_ = std::move(other.music_stream_);

        other.archive_ = nullptr;
        other.start_log_ = nullptr;
        other.sdl_audio_device_ = 0;
    }
    return *this;
}

DecodedClip AudioMixer::decode_sound_clip(const ants::assets::SoundClip& src) {
    DecodedClip out;
    const uint16_t channels = src.format.channels;
    const uint16_t bits = src.format.bits_per_sample;
    const uint32_t rate = src.format.samples_per_sec;
    if ((channels != 1 && channels != 2) || (bits != 8 && bits != 16) || rate == 0 || src.pcm_data.empty()) {
        return out;
    }

    const size_t bytes_per_sample = static_cast<size_t>(bits / 8u);
    const size_t frame_stride = bytes_per_sample * static_cast<size_t>(channels);
    const size_t num_frames = src.pcm_data.size() / frame_stride;
    if (num_frames == 0) return out;

    out.sample_rate = rate;
    out.channels = channels;
    out.samples.resize(num_frames * static_cast<size_t>(channels));

    const uint8_t* raw = src.pcm_data.data();
    if (bits == 8) {
        for (size_t i = 0; i < out.samples.size(); ++i) {
            out.samples[i] = (static_cast<float>(raw[i]) - 128.0f) / 128.0f;
        }
    } else {
        for (size_t i = 0; i < out.samples.size(); ++i) {
            const uint16_t u = static_cast<uint16_t>(
                static_cast<uint32_t>(raw[i * 2u]) |
                (static_cast<uint32_t>(raw[i * 2u + 1u]) << 8)
            );
            int16_t s = 0;
            std::memcpy(&s, &u, sizeof(s));
            out.samples[i] = static_cast<float>(s) / 32768.0f;
        }
    }

    // 1. Per-channel DC-offset removal (e.g., eliminates +16.14% DC bias on sound 79 waterattack.wav)
    double sum[2] = {0.0, 0.0};
    for (size_t f = 0; f < num_frames; ++f) {
        for (uint16_t c = 0; c < channels; ++c) {
            sum[c] += static_cast<double>(out.samples[f * channels + c]);
        }
    }
    float max_abs = 0.0f;
    for (uint16_t c = 0; c < channels; ++c) {
        const float dc = static_cast<float>(sum[c] / static_cast<double>(num_frames));
        for (size_t f = 0; f < num_frames; ++f) {
            float& s = out.samples[f * channels + c];
            s -= dc;
            max_abs = std::max(max_abs, std::abs(s));
        }
    }
    if (max_abs > 1.0f) {
        const float inv = 1.0f / max_abs;
        for (float& s : out.samples) {
            s *= inv;
        }
    }

    // 2. 2.5 ms raised-cosine edge anti-click taper on boundaries exceeding 0.03 FS (fixes 63/67/68 edge steps)
    constexpr float kEdgeClickThreshold = 0.03f;
    constexpr double kEdgeTaperSeconds = 0.0025;
    if (num_frames >= 2u) {
        const size_t max_taper = std::max<size_t>(1u, num_frames / 4u);
        const size_t desired_taper = std::max<size_t>(2u, static_cast<size_t>(static_cast<double>(rate) * kEdgeTaperSeconds + 0.5));
        const size_t taper_frames = std::min<size_t>(max_taper, desired_taper);

        float start_peak = 0.0f;
        float end_peak = 0.0f;
        for (uint16_t c = 0; c < channels; ++c) {
            start_peak = std::max(start_peak, std::abs(out.samples[c]));
            end_peak = std::max(end_peak, std::abs(out.samples[(num_frames - 1u) * channels + c]));
        }

        if (start_peak > kEdgeClickThreshold) {
            for (size_t f = 0; f < taper_frames; ++f) {
                const float t = static_cast<float>(f) / static_cast<float>(taper_frames);
                const float gain = 0.5f * (1.0f - std::cos(kPi * t));
                for (uint16_t c = 0; c < channels; ++c) {
                    out.samples[f * channels + c] *= gain;
                }
            }
        }
        if (end_peak > kEdgeClickThreshold) {
            for (size_t k = 0; k < taper_frames; ++k) {
                const size_t f = num_frames - 1u - k;
                const float t = static_cast<float>(k) / static_cast<float>(taper_frames);
                const float gain = 0.5f * (1.0f - std::cos(kPi * t));
                for (uint16_t c = 0; c < channels; ++c) {
                    out.samples[f * channels + c] *= gain;
                }
            }
        }
    }

    return out;
}

bool AudioMixer::init(const ants::assets::AssetArchive& archive, uint32_t output_sample_rate) {
    // Pre-decode all clips outside mixer_mutex_ so audio callback is never stalled during re-init
    const size_t count = archive.sound_count();
    std::vector<std::shared_ptr<const DecodedClip>> decoded;
    decoded.reserve(count);
    for (size_t i = 0; i < count; ++i) {
        auto clip = std::make_shared<DecodedClip>(decode_sound_clip(archive.get_sound(static_cast<uint32_t>(i))));
        decoded.push_back(std::move(clip));
    }

    std::vector<std::shared_ptr<const DecodedClip>> old_base;
    std::vector<std::shared_ptr<const DecodedClip>> old_overrides;
    {
        std::lock_guard<std::mutex> lock(mixer_mutex_);
        archive_ = &archive;
        output_sample_rate_ = output_sample_rate > 0 ? output_sample_rate : AUDIO_DEFAULT_SAMPLE_RATE;
        old_base = std::move(base_clips_);
        old_overrides = std::move(override_clips_);
        base_clips_ = std::move(decoded);
        override_clips_.assign(count, nullptr);
        for (auto& ch : channels_) {
            ch.active = false;
            ch.clip.reset();
        }
        for (auto& tail : release_tails_) {
            tail.active = false;
            tail.clip.reset();
        }
    }
    return true;
}

bool AudioMixer::set_clip_override(uint32_t sound_id, std::vector<float> samples, uint32_t sample_rate, uint16_t channels) {
    if (sample_rate == 0 || (channels != 1 && channels != 2) || samples.empty() || (samples.size() % channels) != 0) {
        return false;
    }

    auto replacement = std::make_shared<DecodedClip>();
    replacement->samples = std::move(samples);
    replacement->sample_rate = sample_rate;
    replacement->channels = channels;

    std::shared_ptr<const DecodedClip> previous;
    {
        std::lock_guard<std::mutex> lock(mixer_mutex_);
        if (sound_id >= override_clips_.size()) return false;
        previous = std::move(override_clips_[sound_id]);
        override_clips_[sound_id] = std::move(replacement);
    }
    return true;
}

void AudioMixer::clear_clip_override(uint32_t sound_id) {
    std::shared_ptr<const DecodedClip> previous;
    {
        std::lock_guard<std::mutex> lock(mixer_mutex_);
        if (sound_id < override_clips_.size()) {
            previous = std::move(override_clips_[sound_id]);
        }
    }
}

void AudioMixer::clear_clip_overrides() {
    std::vector<std::shared_ptr<const DecodedClip>> old_overrides;
    {
        std::lock_guard<std::mutex> lock(mixer_mutex_);
        old_overrides.swap(override_clips_);
        override_clips_.assign(base_clips_.size(), nullptr);
    }
}

bool AudioMixer::has_clip_override(uint32_t sound_id) const {
    std::lock_guard<std::mutex> lock(mixer_mutex_);
    return sound_id < override_clips_.size() && override_clips_[sound_id] != nullptr;
}

std::shared_ptr<const DecodedClip> AudioMixer::active_clip(uint32_t sound_id) const {
    std::lock_guard<std::mutex> lock(mixer_mutex_);
    return resolve_clip_locked(sound_id);
}

std::shared_ptr<const DecodedClip> AudioMixer::resolve_clip_locked(uint32_t sound_id) const {
    if (sound_id < override_clips_.size() && override_clips_[sound_id]) {
        return override_clips_[sound_id];
    }
    if (sound_id < base_clips_.size()) {
        return base_clips_[sound_id];
    }
    return nullptr;
}

bool AudioMixer::init_sdl_audio(uint16_t buffer_samples) {
    if (headless_mode_.load(std::memory_order_relaxed)) return true;

#if defined(SDL_INIT_AUDIO)
    if (SDL_InitSubSystem(SDL_INIT_AUDIO) < 0) {
        std::cerr << "[AudioMixer] SDL_InitSubSystem(AUDIO) failed: " << SDL_GetError() << "\n";
        return false;
    }

    SDL_AudioSpec desired{}, obtained{};
    desired.freq = static_cast<int>(output_sample_rate_);
    desired.format = AUDIO_S16SYS;
    desired.channels = 2;
    desired.samples = buffer_samples;
    desired.callback = sdl_audio_callback;
    desired.userdata = this;

    sdl_audio_device_ = SDL_OpenAudioDevice(nullptr, 0, &desired, &obtained, 0);
    if (sdl_audio_device_ == 0) {
        std::cerr << "[AudioMixer] SDL_OpenAudioDevice failed: " << SDL_GetError() << "\n";
        return false;
    }

    {
        std::lock_guard<std::mutex> lock(mixer_mutex_);
        output_sample_rate_ = static_cast<uint32_t>(obtained.freq);
        for (auto& ch : channels_) {
            if (ch.active && ch.clip && !ch.clip->empty()) {
                ch.rate_step = static_cast<double>(ch.clip->sample_rate) / static_cast<double>(output_sample_rate_);
                configure_channel_lpf(ch.clip->sample_rate, output_sample_rate_, ch.lpf_enabled, ch.lpf_alpha);
            }
        }
    }
    SDL_PauseAudioDevice(sdl_audio_device_, 0); // Unpause audio
    return true;
#else
    (void)buffer_samples;
    return false;
#endif
}

void AudioMixer::shutdown_sdl_audio() {
#if defined(SDL_INIT_AUDIO)
    if (sdl_audio_device_ != 0) {
        SDL_CloseAudioDevice(sdl_audio_device_);
        sdl_audio_device_ = 0;
    }
#endif
}

void AudioMixer::spawn_release_tail(const MixerChannel& ch) {
    if (!ch.active || !ch.clip || ch.clip->empty() || ch.cursor <= 0.0) return;
    if (ch.mix_left <= 0.0001f && ch.mix_right <= 0.0001f) return;

    size_t slot = AUDIO_MIXER_MAX_RELEASE_TAILS;
    float lowest_fade = 2.0f;
    size_t steal_slot = 0;
    for (size_t i = 0; i < release_tails_.size(); ++i) {
        if (!release_tails_[i].active) {
            slot = i;
            break;
        }
        if (release_tails_[i].fade_gain < lowest_fade) {
            lowest_fade = release_tails_[i].fade_gain;
            steal_slot = i;
        }
    }
    if (slot == AUDIO_MIXER_MAX_RELEASE_TAILS) {
        slot = steal_slot;
    }

    ReleaseTail& tail = release_tails_[slot];
    tail.active = true;
    tail.clip = ch.clip;
    tail.cursor = ch.cursor;
    tail.rate_step = ch.rate_step;
    tail.loop = ch.loop;
    tail.lpf_enabled = ch.lpf_enabled;
    tail.lpf_alpha = ch.lpf_alpha;
    tail.lpf_l = ch.lpf_l;
    tail.lpf_r = ch.lpf_r;
    tail.mix_left = ch.mix_left;
    tail.mix_right = ch.mix_right;
    tail.fade_gain = 1.0f;
    tail.fade_step = 1000.0f / (AUDIO_RELEASE_TAIL_MS * static_cast<float>(output_sample_rate_));
}

int AudioMixer::allocate_channel(uint8_t priority) {
    // 1. Search for first inactive channel
    for (size_t i = 0; i < channels_.size(); ++i) {
        if (!channels_[i].active) return static_cast<int>(i);
    }

    // 2. All 32 channels busy: search for lowest priority channel
    int lowest_idx = -1;
    uint8_t min_prio = 255;
    double max_progress = 0.0;

    for (size_t i = 0; i < channels_.size(); ++i) {
        const size_t total_frames = (channels_[i].clip && !channels_[i].clip->empty()) ? channels_[i].clip->frame_count() : 0u;
        if (channels_[i].priority < min_prio) {
            min_prio = channels_[i].priority;
            lowest_idx = static_cast<int>(i);
            max_progress = (total_frames > 0) ? (channels_[i].cursor / static_cast<double>(total_frames)) : 0.0;
        } else if (channels_[i].priority == min_prio && total_frames > 0) {
            const double prog = channels_[i].cursor / static_cast<double>(total_frames);
            if (prog > max_progress) {
                max_progress = prog;
                lowest_idx = static_cast<int>(i);
            }
        }
    }

    // Preempt if new priority is strictly greater or if tied and past 75% played
    if (lowest_idx >= 0 && (priority > min_prio || (priority == min_prio && max_progress >= 0.75))) {
        return lowest_idx;
    }

    return -1; // Drop sound
}

int AudioMixer::play_sfx(uint32_t sound_id, float volume, uint8_t priority, bool loop, uint32_t owner) {
    std::shared_ptr<const DecodedClip> old_clip;
    int ch_idx = -1;
    {
        std::lock_guard<std::mutex> lock(mixer_mutex_);
        auto clip = resolve_clip_locked(sound_id);
        if (!clip || clip->empty()) return -1;

        ch_idx = allocate_channel(priority);
        if (ch_idx < 0) return -1;

        MixerChannel& ch = channels_[static_cast<size_t>(ch_idx)];
        if (ch.active) {
            spawn_release_tail(ch);
        }
        old_clip = std::move(ch.clip);
        ch.active = true;
        ch.clip = std::move(clip);
        ch.cursor = 0.0;
        ch.rate_step = static_cast<double>(ch.clip->sample_rate) / static_cast<double>(output_sample_rate_);
        configure_channel_lpf(ch.clip->sample_rate, output_sample_rate_, ch.lpf_enabled, ch.lpf_alpha);
        ch.lpf_l = 0.0f;
        ch.lpf_r = 0.0f;
        ch.event_gain = std::clamp(volume, 0.0f, 1.0f);
        ch.owner = owner;
        ch.priority = priority;
        ch.loop = loop;
        if (start_log_) start_log_->push_back({sound_id, owner, 0, 0, false, priority});
        ch.spatial = false;                                // a cue or a UI sound: plain volume 100 %, pan 0 (FUN_0102e883), only the Sound Volume option applies
        apply_law(ch);
    }
    return ch_idx;
}

int AudioMixer::play_spatial(uint32_t sound_id, int32_t world_x, int32_t world_y, uint8_t priority, float volume, bool loop, uint32_t owner) {
    std::shared_ptr<const DecodedClip> old_clip;
    int ch_idx = -1;
    {
        std::lock_guard<std::mutex> lock(mixer_mutex_);
        if (sound_volume_ <= 0) return -1;                 // the option at 0 is -100 dB: nothing to hear, nothing to occupy a channel with
        auto clip = resolve_clip_locked(sound_id);
        if (!clip || clip->empty()) return -1;

        ch_idx = allocate_channel(priority);
        if (ch_idx < 0) return -1;

        MixerChannel& ch = channels_[static_cast<size_t>(ch_idx)];
        if (ch.active) {
            spawn_release_tail(ch);
        }
        old_clip = std::move(ch.clip);
        ch.active = true;
        ch.clip = std::move(clip);
        ch.cursor = 0.0;
        ch.rate_step = static_cast<double>(ch.clip->sample_rate) / static_cast<double>(output_sample_rate_);
        configure_channel_lpf(ch.clip->sample_rate, output_sample_rate_, ch.lpf_enabled, ch.lpf_alpha);
        ch.lpf_l = 0.0f;
        ch.lpf_r = 0.0f;
        ch.event_gain = std::clamp(volume, 0.0f, 1.0f);
        ch.owner = owner;
        ch.priority = priority;
        ch.loop = loop;
        if (start_log_) start_log_->push_back({sound_id, owner, world_x, world_y, true, priority});
        ch.spatial = true;
        ch.world_x = world_x;
        ch.world_y = world_y;
        apply_law(ch);                                     // no culling: a sound on the far side of the map is faint (-10 dB at 1000 px), not gone
    }
    return ch_idx;
}

void AudioMixer::stop_all() {
    const bool headless = headless_mode_.load(std::memory_order_relaxed);
    std::lock_guard<std::mutex> lock(mixer_mutex_);
    for (auto& ch : channels_) {
        if (ch.active && !headless) {
            spawn_release_tail(ch);
        }
        ch.active = false;
    }
    if (headless) {
        for (auto& tail : release_tails_) {
            tail.active = false;
        }
    }
}

void AudioMixer::stop_owner(uint32_t owner) {
    if (owner == 0) return;
    std::lock_guard<std::mutex> lock(mixer_mutex_);
    for (auto& ch : channels_) {
        if (ch.active && ch.owner == owner) {
            spawn_release_tail(ch);
            ch.active = false;
        }
    }
}

bool AudioMixer::is_channel_active(int channel_id) const {
    if (channel_id < 0 || channel_id >= static_cast<int>(AUDIO_MIXER_MAX_CHANNELS)) return false;
    std::lock_guard<std::mutex> lock(mixer_mutex_);
    return channels_[static_cast<size_t>(channel_id)].active;
}

size_t AudioMixer::active_channel_count() const {
    std::lock_guard<std::mutex> lock(mixer_mutex_);
    size_t count = 0;
    for (const auto& ch : channels_) {
        if (ch.active) ++count;
    }
    return count;
}

size_t AudioMixer::active_release_tail_count() const {
    std::lock_guard<std::mutex> lock(mixer_mutex_);
    size_t count = 0;
    for (const auto& tail : release_tails_) {
        if (tail.active) ++count;
    }
    return count;
}

void AudioMixer::set_listener_position(int32_t world_x, int32_t world_y) {
    std::lock_guard<std::mutex> lock(mixer_mutex_);
    if (world_x == listener_x_ && world_y == listener_y_) return;
    listener_x_ = world_x;
    listener_y_ = world_y;

    // The playing positional sounds follow the moving view (FUN_0102f777 re-attenuates every playing sound)
    for (auto& ch : channels_) {
        if (ch.active && ch.spatial) apply_law(ch, false);       // (the sound goes on from where it is: its gains ramp to the new law)
    }
}

// ------------------------------------------------------------------------------------------------
// The sound law of the original
// ------------------------------------------------------------------------------------------------

int32_t AudioMixer::distance_percent(int32_t dx, int32_t dy) noexcept {
    const int32_t ax = dx < 0 ? -dx : dx;
    const int32_t ay = dy < 0 ? -dy : dy;
    const int32_t m = (ax <= ay) ? ay : ax;                       // 0x102e913: the larger of the two distances
    const int32_t pct = 100 - (m * 100) / AUDIO_LISTENER_RADIUS;   // imul 100, idiv radius: truncating
    return pct < 0 ? 0 : pct;                                     // (beyond the radius the binary hands DirectSound an invalid volume; no map is that large)
}

int32_t AudioMixer::pan_centibels(int32_t dx) noexcept {
    const int32_t steps = (dx * 100) / AUDIO_LISTENER_RADIUS;      // trunc(dx / 25)
    return (steps * 2500) / 100;                                  // imul 0x9c4, idiv 100: 25 per step, range +-2500
}

int32_t AudioMixer::attenuation_centibels(int32_t sound_volume, int32_t pct) noexcept {
    if (sound_volume <= 0) return -10000;                         // SetSoundVolume(0): the sounds are muted (0x102d80c)
    const int32_t sv = sound_volume > 100 ? 100 : sound_volume;
    const int32_t p = pct < 0 ? 0 : (pct > 100 ? 100 : pct);
    return ((sv * p) / 100 - 100) * 25;                           // 0x102d813: imul, div 100, sub 100, imul 2500, idiv 100
}

float AudioMixer::gain_from_centibels(int32_t centibels) noexcept {
    if (centibels <= -10000) return 0.0f;
    if (centibels >= 0) return 1.0f;
    return static_cast<float>(std::pow(10.0, static_cast<double>(centibels) / 2000.0));
}

float AudioMixer::soft_limit_sample(float x) noexcept {
    const float ax = std::abs(x);
    if (ax <= AUDIO_LIMITER_KNEE) return x;
    constexpr float kHeadroom = 1.0f - AUDIO_LIMITER_KNEE;
    const float compressed = AUDIO_LIMITER_KNEE + kHeadroom * std::tanh((ax - AUDIO_LIMITER_KNEE) / kHeadroom);
    return (x >= 0.0f) ? compressed : -compressed;
}

uint8_t AudioMixer::sim_event_priority(uint32_t sound_id, uint8_t base_priority) noexcept {
    namespace S = ants::sim::SoundID;
    uint8_t tier = 30;
    switch (sound_id) {
        // Critical alerts, match timer warnings, alliance cues, dropouts, win/loss stings
        case S::BaseAlarmSiren:
        case S::Anthill:
        case S::OneMinute:
        case S::ThirtySeconds:
        case S::Countdown:
        case S::PlayerDropOut:
        case S::PlayerDefeat:
        case S::VictoryFanfare:
        case S::AlliancePro:
        case S::AllianceYes:
        case S::AllianceNot:
        case S::AllianceOn:
        case S::AllianceBreak:
            tier = 220;
            break;

        // High-impact explosions, score updates, powerups, command refusals
        case S::BombDetonate:
        case S::BaseScoreUp:
        case S::BaseScoreDn:
        case S::PowerUpDrop:
        case S::PowerUpHeal:
        case S::PowerUpChime:
        case S::AntStop:
        case S::CantGo:
            tier = 160;
            break;

        // Combat strikes, fire, water splashes, bomb/thief actions, knockback impacts
        case S::CombatNetFairy:
        case S::MeleeAttack:
        case S::AttackAlt:
        case S::HeavyPunch:
        case S::WaterAttack:
        case S::ThiefWhip:
        case S::FireBeam:
        case S::FireErupt:
        case S::FireExtinguish:
        case S::FireBurnout:
        case S::WaterSplash:
        case S::AntDrown:
        case S::BombPick:
        case S::BombDefuseGrab:
        case S::BombBodySquash:
        case S::ThiefDive:
        case S::ThiefRummage:
        case S::ThiefEmerge:
        case S::FlingThumpA:
        case S::FlingThumpB:
        case S::StunRecover:
            tier = 100;
            break;

        // Normal unit work / emergence
        case S::ExitHill:
        case S::FoodHarvest:
        case S::FoodGrab:
        case S::ShovelGravel:
        case S::ShovelWater:
        case 40u:  // powerupd.wav
        case 76u:  // FlyThumpB duplicate
        case 80u:  // wateraction.wav
            tier = 60;
            break;

        // Low-priority repetitive ambient collisions
        case S::Bump:
        default:
            tier = 30;
            break;
    }
    return std::max(base_priority, tier);
}

// The gains of a channel: DirectSound SetVolume(att) on both channels, SetPan(pan): a positive pan attenuates the left channel by `pan`, a negative one the right channel.
void AudioMixer::apply_law(MixerChannel& ch, bool snap) const {
    int32_t pct = 100;
    int32_t pan = 0;
    if (ch.spatial) {
        const int32_t dx = ch.world_x - listener_x_;
        const int32_t dy = ch.world_y - listener_y_;
        pct = distance_percent(dx, dy);
        pan = pan_centibels(dx);
    }
    const float volume = gain_from_centibels(attenuation_centibels(sound_volume_, pct)) * ch.event_gain;
    const float left = pan > 0 ? gain_from_centibels(-pan) : 1.0f;
    const float right = pan < 0 ? gain_from_centibels(pan) : 1.0f;
    ch.vol_left = volume * left;
    ch.vol_right = volume * right;
    if (snap) {
        ch.mix_left = ch.vol_left;
        ch.mix_right = ch.vol_right;
    }
}

void AudioMixer::calculate_spatial_pan(int32_t world_x, int32_t world_y, float base_vol, float& out_vol_l, float& out_vol_r) const {
    MixerChannel probe;
    probe.spatial = true;
    probe.world_x = world_x;
    probe.world_y = world_y;
    probe.event_gain = base_vol;
    apply_law(probe);
    out_vol_l = probe.vol_left;
    out_vol_r = probe.vol_right;
}

void AudioMixer::set_sound_volume(int32_t sound_volume) {
    std::lock_guard<std::mutex> lock(mixer_mutex_);
    sound_volume_ = std::clamp<int32_t>(sound_volume, 0, 100);
    for (auto& ch : channels_) {
        if (ch.active) apply_law(ch, false); // Ramp smoothly over AUDIO_GAIN_RAMP_MS to avoid volume step clicks
    }
}

void AudioMixer::set_output_gain(float gain) {
    std::lock_guard<std::mutex> lock(mixer_mutex_);
    output_gain_target_ = std::clamp(gain, 0.0f, 1.0f);
}

float AudioMixer::output_gain() const {
    std::lock_guard<std::mutex> lock(mixer_mutex_);
    return output_gain_target_;
}

void AudioMixer::set_sfx_volume(float volume) {
    set_sound_volume(static_cast<int32_t>(std::clamp(volume, 0.0f, 1.0f) * 100.0f + 0.5f));
}

bool AudioMixer::channel_volumes(int channel_id, float& left, float& right) const {
    if (channel_id < 0 || channel_id >= static_cast<int>(AUDIO_MIXER_MAX_CHANNELS)) return false;
    std::lock_guard<std::mutex> lock(mixer_mutex_);
    const MixerChannel& ch = channels_[static_cast<size_t>(channel_id)];
    if (!ch.active) return false;
    left = ch.vol_left;
    right = ch.vol_right;
    return true;
}

void AudioMixer::ingest_simulation_events(const std::vector<ants::sim::AudioEvent>& events, uint8_t local_player_id) {
    std::array<uint8_t, 128> same_tick_counts{};
    for (const auto& ev : events) {
        // Filter targeted audio events (e.g. thief base siren targeted to victim)
        if (ev.target_player != 255 && ev.target_player != local_player_id) {
            continue;
        }
        if (ev.stop) {                                   // the owner's clip was replaced or the owner is gone: what it started is cut
            stop_owner(ev.owner);
            continue;
        }

        const uint8_t effective_priority = sim_event_priority(ev.sound_id, ev.priority);
        uint8_t dup_index = 0;
        if (ev.sound_id < same_tick_counts.size()) {
            dup_index = same_tick_counts[ev.sound_id];
            if (same_tick_counts[ev.sound_id] < 255u) {
                ++same_tick_counts[ev.sound_id];
            }
        }
        // Damp phase-locked identical triggers within the same 20 Hz tick (1.0, 0.5, 0.333, ...) while keeping all channels & start_log intact
        const float event_gain = 1.0f / static_cast<float>(static_cast<uint32_t>(dup_index) + 1u);

        // Check if sound is non-spatial, broadcast, or player score notification
        if ((ev.world_x == 0 && ev.world_y == 0) ||
            ev.sound_id == ants::sim::SoundID::BaseScoreUp ||
            ev.sound_id == ants::sim::SoundID::BaseScoreDn) {
            play_sfx(ev.sound_id, event_gain, effective_priority, false, ev.owner);
        } else {
            play_spatial(ev.sound_id, ev.world_x, ev.world_y, effective_priority, event_gain, false, ev.owner);
        }
    }
}

bool AudioMixer::play_music(const std::string& filepath, bool loop) {
    const bool headless = headless_mode_.load(std::memory_order_relaxed);
    auto next = std::make_unique<MusicStream>();

    // Perform all disk I/O and MP3 header initialization completely outside mixer_mutex_
    std::string candidate = filepath;
    std::ifstream f(candidate, std::ios::binary | std::ios::ate);
    if (!f.is_open()) {
        std::string fb1 = std::string("Original-Ants/") + filepath;
        f.open(fb1, std::ios::binary | std::ios::ate);
        if (f.is_open()) {
            candidate = fb1;
        } else {
            std::string fb2 = std::string("../") + filepath;
            f.open(fb2, std::ios::binary | std::ios::ate);
            if (f.is_open()) {
                candidate = fb2;
            }
        }
    }

    bool ok = false;
    if (!f.is_open()) {
        if (headless) {
            next->is_playing = true;
            next->loop = loop;
            next->filepath = filepath;
            ok = true;
        } else {
            std::cerr << "[AudioMixer] Failed to open music file: " << filepath << "\n";
        }
    } else {
        const std::streamsize size = static_cast<std::streamsize>(f.tellg());
        if (size > 0) {
            f.seekg(0, std::ios::beg);
            next->file_data.resize(static_cast<size_t>(size));
            if (f.read(reinterpret_cast<char*>(next->file_data.data()), size)) {
                f.close();
                if (drmp3_init_memory(&next->decoder,
                                      next->file_data.data(),
                                      next->file_data.size(),
                                      nullptr)) {
                    next->is_open = true;
                    next->is_playing = true;
                    next->is_paused = false;
                    next->stopping = false;
                    next->loop = loop;
                    next->fade_volume = 1.0f;
                    next->gain_now = 0.0f; // Snapped to target volume on swap under mixer_mutex_
                    next->is_fading = false;
                    next->filepath = filepath;
                    next->reset_src();
                    ok = true;
                } else {
                    next->file_data.clear();
                    std::cerr << "[AudioMixer] Failed to decode MP3 header: " << candidate << "\n";
                }
            } else {
                next->file_data.clear();
            }
        }
    }

    // Swap into place under mixer_mutex_ and let the old MusicStream destruct off-lock
    std::unique_ptr<MusicStream> previous;
    {
        std::lock_guard<std::mutex> lock(mixer_mutex_);
        if (music_stream_) {
            next->volume = music_stream_->volume;
        }
        if (next->is_open) {
            next->gain_now = next->volume * next->fade_volume * AUDIO_MUSIC_MAKEUP_GAIN;
        }
        previous = std::move(music_stream_);
        music_stream_ = std::move(next);
    }
    return ok;
}

void AudioMixer::stop_music() {
    const bool headless = headless_mode_.load(std::memory_order_relaxed);
    std::unique_ptr<MusicStream> to_destroy;
    {
        std::lock_guard<std::mutex> lock(mixer_mutex_);
        if (!music_stream_) return;
        music_stream_->is_playing = false;
        music_stream_->is_paused = false;
        music_stream_->is_fading = false;
        music_stream_->fade_volume = 1.0f;
        if (music_stream_->is_open && !headless && music_stream_->gain_now > 0.0001f) {
            // Smoothly ramp down in the audio callback; old stream is freed on next play_music() or destructor
            music_stream_->stopping = true;
        } else {
            auto replacement = std::make_unique<MusicStream>();
            replacement->volume = music_stream_->volume;
            replacement->filepath = music_stream_->filepath;
            to_destroy = std::move(music_stream_);
            music_stream_ = std::move(replacement);
        }
    }
}

void AudioMixer::pause_music() {
    std::lock_guard<std::mutex> lock(mixer_mutex_);
    if (music_stream_ && music_stream_->is_playing) {
        music_stream_->is_paused = true;
    }
}

void AudioMixer::resume_music() {
    std::lock_guard<std::mutex> lock(mixer_mutex_);
    if (music_stream_ && music_stream_->is_playing) {
        music_stream_->is_paused = false;
    }
}

void AudioMixer::set_music_volume(float volume) {
    std::lock_guard<std::mutex> lock(mixer_mutex_);
    if (music_stream_) {
        music_stream_->volume = std::clamp(volume, 0.0f, 1.0f);
    }
}

float AudioMixer::get_music_volume() const noexcept {
    std::lock_guard<std::mutex> lock(mixer_mutex_);
    return music_stream_ ? music_stream_->volume : 0.8f;
}

std::string AudioMixer::music_filepath() const {
    std::lock_guard<std::mutex> lock(mixer_mutex_);
    return music_stream_ ? music_stream_->filepath : std::string();
}

bool AudioMixer::music_loops() const {
    std::lock_guard<std::mutex> lock(mixer_mutex_);
    return music_stream_ && music_stream_->loop;
}

bool AudioMixer::is_music_playing() const {
    std::lock_guard<std::mutex> lock(mixer_mutex_);
    if (!music_stream_) return false;
    return music_stream_->is_playing && !music_stream_->is_paused;
}

void AudioMixer::fade_out_music(float duration_sec) {
    std::lock_guard<std::mutex> lock(mixer_mutex_);
    if (!music_stream_ || !music_stream_->is_playing) return;
    if (duration_sec <= 0.001f) {
        music_stream_->fade_volume = 0.0f;
        music_stream_->is_playing = false;
        music_stream_->is_fading = false;
    } else {
        music_stream_->is_fading = true;
        music_stream_->fade_duration = duration_sec;
        music_stream_->fade_timer = duration_sec;
    }
}

void AudioMixer::update_music(float dt) {
    std::lock_guard<std::mutex> lock(mixer_mutex_);
    if (!music_stream_) return;

    if (music_stream_->is_playing && music_stream_->is_fading) {
        music_stream_->fade_timer -= dt;
        if (music_stream_->fade_timer <= 0.0f) {
            music_stream_->fade_volume = 0.0f;
            music_stream_->is_playing = false;
            music_stream_->is_fading = false;
        } else if (music_stream_->fade_duration > 0.0001f) {
            music_stream_->fade_volume = std::clamp(music_stream_->fade_timer / music_stream_->fade_duration, 0.0f, 1.0f);
        }
    }
}

void AudioMixer::mix_samples_i16(int16_t* out_stereo, size_t num_frames) {
    std::lock_guard<std::mutex> lock(mixer_mutex_);
    if (!out_stereo || num_frames == 0) return;
    std::memset(out_stereo, 0, num_frames * 2u * sizeof(int16_t));

    size_t offset = 0;
    while (offset < num_frames) {
        const size_t chunk = std::min<size_t>(kMixChunkFrames, num_frames - offset);
        mix_chunk_i16_locked(out_stereo + offset * 2u, chunk);
        offset += chunk;
    }
}

void AudioMixer::mix_chunk_i16_locked(int16_t* out_stereo, size_t chunk_frames) {
    // Decode music chunk into fixed stack buffer (zero heap allocation regardless of total frame count)
    float stack_music_buf[kMixChunkFrames * 2];
    bool has_music = false;
    float music_target_gain = 0.0f;
    float music_ramp_step = 0.0f;

    if (music_stream_ && music_stream_->is_open) {
        const bool active_stream = (music_stream_->is_playing && !music_stream_->is_paused && !music_stream_->stopping);
        const bool ramping_down = ((music_stream_->is_paused || music_stream_->stopping) && music_stream_->gain_now > 0.0001f);
        if (active_stream || ramping_down) {
            music_target_gain = active_stream ? (music_stream_->volume * music_stream_->fade_volume * AUDIO_MUSIC_MAKEUP_GAIN) : 0.0f;
            const float ramp_ms = active_stream ? AUDIO_GAIN_RAMP_MS : AUDIO_RELEASE_TAIL_MS;
            music_ramp_step = AUDIO_MUSIC_MAKEUP_GAIN * 1000.0f / (ramp_ms * static_cast<float>(output_sample_rate_));
            music_stream_->read_stereo_chunk(stack_music_buf, chunk_frames, output_sample_rate_);
            has_music = true;
        } else if (music_stream_->stopping) {
            music_stream_->stopping = false;
            music_stream_->gain_now = 0.0f;
        }
    }

    const float ramp_step = 1000.0f / (AUDIO_GAIN_RAMP_MS * static_cast<float>(output_sample_rate_));      // the change of one output frame
    const float sfx_gain = 1.0f;                 // the Sound Volume option is inside every channel's gain (apply_law)

    for (size_t f = 0; f < chunk_frames; ++f) {
        float mix_l = 0.0f;
        float mix_r = 0.0f;

        if (sfx_gain > 0.0001f) {
            for (auto& ch : channels_) {
                if (!ch.active || !ch.clip || ch.clip->empty()) continue;

                const size_t total_frames = ch.clip->frame_count();
                if (ch.cursor < static_cast<double>(total_frames)) {
                    float sample_l = 0.0f;
                    float sample_r = 0.0f;
                    sample_clip_hermite_lpf(*ch.clip, ch.cursor, ch.loop, ch.lpf_enabled, ch.lpf_alpha, ch.lpf_l, ch.lpf_r, sample_l, sample_r);

                    ch.mix_left += std::clamp(ch.vol_left - ch.mix_left, -ramp_step, ramp_step);
                    ch.mix_right += std::clamp(ch.vol_right - ch.mix_right, -ramp_step, ramp_step);
                    mix_l += sample_l * ch.mix_left;
                    mix_r += sample_r * ch.mix_right;

                    ch.cursor += ch.rate_step;
                    if (ch.cursor >= static_cast<double>(total_frames)) {
                        if (ch.loop) {
                            const double d_frames = static_cast<double>(total_frames);
                            while (ch.cursor >= d_frames) {
                                ch.cursor -= d_frames;
                            }
                        } else {
                            ch.active = false;
                        }
                    }
                } else {
                    ch.active = false;
                }
            }

            // Mix bounded de-click release tails (never decrements shared_ptr on audio thread)
            for (auto& tail : release_tails_) {
                if (!tail.active || !tail.clip || tail.clip->empty()) continue;

                const size_t total_frames = tail.clip->frame_count();
                if (tail.cursor < static_cast<double>(total_frames) && tail.fade_gain > 0.0f) {
                    float sample_l = 0.0f;
                    float sample_r = 0.0f;
                    sample_clip_hermite_lpf(*tail.clip, tail.cursor, tail.loop, tail.lpf_enabled, tail.lpf_alpha, tail.lpf_l, tail.lpf_r, sample_l, sample_r);

                    mix_l += sample_l * tail.mix_left * tail.fade_gain;
                    mix_r += sample_r * tail.mix_right * tail.fade_gain;

                    tail.fade_gain -= tail.fade_step;
                    tail.cursor += tail.rate_step;
                    if (tail.fade_gain <= 0.0f) {
                        tail.fade_gain = 0.0f;
                        tail.active = false;
                    } else if (tail.cursor >= static_cast<double>(total_frames)) {
                        if (tail.loop) {
                            const double d_frames = static_cast<double>(total_frames);
                            while (tail.cursor >= d_frames) {
                                tail.cursor -= d_frames;
                            }
                        } else {
                            tail.active = false;
                        }
                    }
                } else {
                    tail.active = false;
                }
            }

            mix_l *= sfx_gain;
            mix_r *= sfx_gain;
        }

        if (has_music) {
            music_stream_->gain_now += std::clamp(music_target_gain - music_stream_->gain_now, -music_ramp_step, music_ramp_step);
            const float mg = music_stream_->gain_now;
            if (mg > 0.0001f) {
                // Clamp sub-16-bit (-88 dBFS) MP3 encoder silence floor to exact 0 before applying makeup gain
                constexpr float kMusicSilenceFloor = 4.0e-5f;
                const float ml = stack_music_buf[f * 2u + 0u];
                const float mr = stack_music_buf[f * 2u + 1u];
                mix_l += (std::abs(ml) >= kMusicSilenceFloor ? ml : 0.0f) * mg;
                mix_r += (std::abs(mr) >= kMusicSilenceFloor ? mr : 0.0f) * mg;
            } else if (music_stream_->stopping) {
                music_stream_->stopping = false;
                music_stream_->gain_now = 0.0f;
            }
        }

        // Soft-knee master bus limiter placed before output_gain_now_ (strictly linear for |x| <= 0.85)
        mix_l = soft_limit_sample(mix_l);
        mix_r = soft_limit_sample(mix_r);

        output_gain_now_ += std::clamp(output_gain_target_ - output_gain_now_, -ramp_step, ramp_step);      // (the settings menu's Master volume and Mute)
        mix_l *= output_gain_now_;
        mix_r *= output_gain_now_;

        mix_l = std::clamp(mix_l, -1.0f, 1.0f);
        mix_r = std::clamp(mix_r, -1.0f, 1.0f);

        out_stereo[f * 2u + 0u] = static_cast<int16_t>(mix_l * 32767.0f);
        out_stereo[f * 2u + 1u] = static_cast<int16_t>(mix_r * 32767.0f);
    }
}

std::vector<int16_t> AudioMixer::render_frames(size_t num_frames) {
    std::vector<int16_t> buffer(num_frames * 2u, 0);
    mix_samples_i16(buffer.data(), num_frames);
    return buffer;
}

} // namespace ants::app
