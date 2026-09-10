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

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wsign-conversion"
#pragma GCC diagnostic ignored "-Wconversion"
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#define DR_MP3_IMPLEMENTATION
#include "ants_app/dr_mp3.h"
#pragma GCC diagnostic pop

namespace ants::app {

struct AudioMixer::MusicStream {
    std::vector<uint8_t> file_data;
    drmp3 decoder{};
    bool is_open{false};
    bool is_playing{false};
    bool is_paused{false};
    bool loop{true};
    float volume{0.8f};
    float fade_volume{1.0f};
    float fade_duration{0.0f};
    float fade_timer{0.0f};
    bool is_fading{false};
    std::string filepath;

    void cleanup() {
        if (is_open) {
            drmp3_uninit(&decoder);
            is_open = false;
        }
        file_data.clear();
        is_playing = false;
        is_paused = false;
        is_fading = false;
        fade_volume = 1.0f;
    }

    ~MusicStream() {
        cleanup();
    }
};

static void sdl_audio_callback(void* userdata, uint8_t* stream, int len) {
    auto* mixer = static_cast<AudioMixer*>(userdata);
    if (!mixer) return;
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
    channels_ = other.channels_;
    listener_x_ = other.listener_x_;
    listener_y_ = other.listener_y_;
    master_volume_ = other.master_volume_;
    sfx_volume_ = other.sfx_volume_;
    headless_mode_ = other.headless_mode_;
    sdl_audio_device_ = other.sdl_audio_device_;
    music_stream_ = std::move(other.music_stream_);

    other.archive_ = nullptr;
    other.sdl_audio_device_ = 0;
}

AudioMixer& AudioMixer::operator=(AudioMixer&& other) noexcept {
    if (this != &other) {
        shutdown_sdl_audio();
        std::scoped_lock lock(mixer_mutex_, other.mixer_mutex_);
        archive_ = other.archive_;
        output_sample_rate_ = other.output_sample_rate_;
        channels_ = other.channels_;
        listener_x_ = other.listener_x_;
        listener_y_ = other.listener_y_;
        master_volume_ = other.master_volume_;
        sfx_volume_ = other.sfx_volume_;
        headless_mode_ = other.headless_mode_;
        sdl_audio_device_ = other.sdl_audio_device_;
        music_stream_ = std::move(other.music_stream_);

        other.archive_ = nullptr;
        other.sdl_audio_device_ = 0;
    }
    return *this;
}

bool AudioMixer::init(const ants::assets::AssetArchive& archive, uint32_t output_sample_rate) {
    std::lock_guard<std::mutex> lock(mixer_mutex_);
    archive_ = &archive;
    output_sample_rate_ = output_sample_rate;
    for (auto& ch : channels_) {
        ch.active = false;
        ch.clip = nullptr;
    }
    return true;
}

bool AudioMixer::init_sdl_audio(uint16_t buffer_samples) {
    if (headless_mode_) return true;

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

    output_sample_rate_ = static_cast<uint32_t>(obtained.freq);
    SDL_PauseAudioDevice(sdl_audio_device_, 0); // Unpause audio
    return true;
#else
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
        if (channels_[i].priority < min_prio) {
            min_prio = channels_[i].priority;
            lowest_idx = static_cast<int>(i);
            if (channels_[i].clip && !channels_[i].clip->pcm_data.empty()) {
                max_progress = channels_[i].cursor / static_cast<double>(channels_[i].clip->pcm_data.size());
            }
        } else if (channels_[i].priority == min_prio && channels_[i].clip && !channels_[i].clip->pcm_data.empty()) {
            double prog = channels_[i].cursor / static_cast<double>(channels_[i].clip->pcm_data.size());
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

int AudioMixer::play_sfx(uint32_t sound_id, float volume, uint8_t priority, bool loop) {
    if (!archive_ || sound_id >= archive_->sound_count()) return -1;
    const auto& clip = archive_->get_sound(sound_id);
    if (clip.pcm_data.empty()) return -1;

    std::lock_guard<std::mutex> lock(mixer_mutex_);
    int ch_idx = allocate_channel(priority);
    if (ch_idx < 0) return -1;

    MixerChannel& ch = channels_[static_cast<size_t>(ch_idx)];
    ch.active = true;
    ch.sound_id = sound_id;
    ch.clip = &clip;
    ch.cursor = 0.0;
    ch.rate_step = static_cast<double>(clip.format.samples_per_sec) / static_cast<double>(output_sample_rate_);
    ch.vol_left = std::clamp(volume, 0.0f, 1.0f);
    ch.vol_right = std::clamp(volume, 0.0f, 1.0f);
    ch.priority = priority;
    ch.loop = loop;
    ch.spatial = false;

    return ch_idx;
}

int AudioMixer::play_spatial(uint32_t sound_id, int32_t world_x, int32_t world_y, uint8_t priority, float volume, bool loop) {
    if (!archive_ || sound_id >= archive_->sound_count()) return -1;
    const auto& clip = archive_->get_sound(sound_id);
    if (clip.pcm_data.empty()) return -1;

    float vl = 1.0f, vr = 1.0f;
    calculate_spatial_pan(world_x, world_y, volume, vl, vr);

    // Cull sounds completely out of audible range
    if (vl <= 0.0001f && vr <= 0.0001f) return -1;

    std::lock_guard<std::mutex> lock(mixer_mutex_);
    int ch_idx = allocate_channel(priority);
    if (ch_idx < 0) return -1;

    MixerChannel& ch = channels_[static_cast<size_t>(ch_idx)];
    ch.active = true;
    ch.sound_id = sound_id;
    ch.clip = &clip;
    ch.cursor = 0.0;
    ch.rate_step = static_cast<double>(clip.format.samples_per_sec) / static_cast<double>(output_sample_rate_);
    ch.vol_left = vl;
    ch.vol_right = vr;
    ch.priority = priority;
    ch.loop = loop;
    ch.spatial = true;
    ch.world_x = world_x;
    ch.world_y = world_y;

    return ch_idx;
}

void AudioMixer::stop_all() {
    std::lock_guard<std::mutex> lock(mixer_mutex_);
    for (auto& ch : channels_) {
        ch.active = false;
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

void AudioMixer::set_listener_position(int32_t world_x, int32_t world_y) {
    std::lock_guard<std::mutex> lock(mixer_mutex_);
    listener_x_ = world_x;
    listener_y_ = world_y;

    // Dynamically update volumes for active spatial sounds
    for (auto& ch : channels_) {
        if (ch.active && ch.spatial) {
            calculate_spatial_pan(ch.world_x, ch.world_y, 1.0f, ch.vol_left, ch.vol_right);
        }
    }
}


void AudioMixer::calculate_spatial_pan(int32_t world_x, int32_t world_y, float base_vol, float& out_vol_l, float& out_vol_r) const {
    float dx = static_cast<float>(world_x - listener_x_);
    float dy = static_cast<float>(world_y - listener_y_);
    float dist = std::sqrt(dx * dx + dy * dy);

    float atten = std::clamp(1.0f - (dist / AUDIO_MAX_AUDIBLE_DISTANCE), 0.0f, 1.0f);

    constexpr float HALF_VIEWPORT_WIDTH = 441.0f * 0.5f;
    float pan = std::clamp(dx / HALF_VIEWPORT_WIDTH, -1.0f, 1.0f);

    // Equal-power stereo panning curve
    float angle = (pan + 1.0f) * 0.25f * 3.14159265358979323846f;
    float pan_l = std::cos(angle);
    float pan_r = std::sin(angle);

    out_vol_l = base_vol * atten * pan_l;
    out_vol_r = base_vol * atten * pan_r;
}


void AudioMixer::set_sfx_volume(float volume) {
    std::lock_guard<std::mutex> lock(mixer_mutex_);
    sfx_volume_ = std::clamp(volume, 0.0f, 1.0f);
}

void AudioMixer::ingest_simulation_events(const std::vector<ants::sim::AudioEvent>& events, uint8_t local_player_id) {
    for (const auto& ev : events) {
        // Filter targeted audio events (e.g. thief base siren targeted to victim)
        if (ev.target_player != 255 && ev.target_player != local_player_id) {
            continue;
        }

        // Check if sound is non-spatial, broadcast, or player score notification
        if ((ev.world_x == 0 && ev.world_y == 0) ||
            ev.sound_id == ants::sim::SoundID::BaseScoreUp ||
            ev.sound_id == ants::sim::SoundID::BaseScoreDn) {
            play_sfx(ev.sound_id, 1.0f, ev.priority);
        } else {
            play_spatial(ev.sound_id, ev.world_x, ev.world_y, ev.priority);
        }
    }
}

bool AudioMixer::play_music(const std::string& filepath, bool loop) {
    std::lock_guard<std::mutex> lock(mixer_mutex_);
    if (!music_stream_) {
        music_stream_ = std::make_unique<MusicStream>();
    }
    music_stream_->cleanup();

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

    if (!f.is_open()) {
        if (headless_mode_) {
            music_stream_->is_playing = true;
            music_stream_->loop = loop;
            music_stream_->filepath = filepath;
            return true;
        }
        std::cerr << "[AudioMixer] Failed to open music file: " << filepath << "\n";
        return false;
    }

    std::streamsize size = static_cast<std::streamsize>(f.tellg());
    f.seekg(0, std::ios::beg);
    music_stream_->file_data.resize(static_cast<size_t>(size));
    if (!f.read(reinterpret_cast<char*>(music_stream_->file_data.data()), size)) {
        music_stream_->file_data.clear();
        return false;
    }
    f.close();

    if (!drmp3_init_memory(&music_stream_->decoder,
                           music_stream_->file_data.data(),
                           music_stream_->file_data.size(),
                           nullptr)) {
        music_stream_->file_data.clear();
        std::cerr << "[AudioMixer] Failed to decode MP3 header: " << candidate << "\n";
        return false;
    }

    music_stream_->is_open = true;
    music_stream_->is_playing = true;
    music_stream_->is_paused = false;
    music_stream_->loop = loop;
    music_stream_->fade_volume = 1.0f;
    music_stream_->is_fading = false;
    music_stream_->filepath = filepath;
    return true;
}

void AudioMixer::stop_music() {
    std::lock_guard<std::mutex> lock(mixer_mutex_);
    if (music_stream_) {
        music_stream_->cleanup();
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
    std::memset(out_stereo, 0, num_frames * 2 * sizeof(int16_t));

    // Decode music frames if music is playing
    std::vector<float> music_buf_storage;
    float* music_buf = nullptr;
    float stack_music_buf[2048 * 2];
    bool has_music = false;
    float music_gain = 0.0f;

    if (music_stream_ && music_stream_->is_open && music_stream_->is_playing && !music_stream_->is_paused) {
        music_gain = master_volume_ * music_stream_->volume * music_stream_->fade_volume;
        uint32_t mp3_channels = music_stream_->decoder.channels;
        if (mp3_channels == 1 || mp3_channels == 2) {
            if (num_frames * mp3_channels <= sizeof(stack_music_buf) / sizeof(float)) {
                music_buf = stack_music_buf;
            } else {
                music_buf_storage.resize(num_frames * mp3_channels);
                music_buf = music_buf_storage.data();
            }

            size_t total_read = 0;
            while (total_read < num_frames) {
                drmp3_uint64 need = static_cast<drmp3_uint64>(num_frames - total_read);
                drmp3_uint64 read = drmp3_read_pcm_frames_f32(&music_stream_->decoder, need, music_buf + total_read * mp3_channels);
                if (read == 0) {
                    if (music_stream_->loop) {
                        drmp3_seek_to_pcm_frame(&music_stream_->decoder, 0);
                        read = drmp3_read_pcm_frames_f32(&music_stream_->decoder, need, music_buf + total_read * mp3_channels);
                        if (read == 0) {
                            break;
                        }
                    } else {
                        music_stream_->is_playing = false;
                        break;
                    }
                }
                total_read += static_cast<size_t>(read);
            }
            if (total_read < num_frames) {
                std::memset(music_buf + total_read * mp3_channels, 0, (num_frames - total_read) * mp3_channels * sizeof(float));
            }
            has_music = (music_gain > 0.0001f);
        }
    }

    if (master_volume_ <= 0.0001f) return;
    const float sfx_gain = master_volume_ * sfx_volume_;

    for (size_t f = 0; f < num_frames; ++f) {
        float mix_l = 0.0f;
        float mix_r = 0.0f;

        if (sfx_gain > 0.0001f) {
            for (auto& ch : channels_) {
                if (!ch.active || !ch.clip || ch.clip->pcm_data.empty()) continue;

                const auto& data = ch.clip->pcm_data;
                size_t idx0 = static_cast<size_t>(ch.cursor);
                double frac = ch.cursor - static_cast<double>(idx0);

                if (idx0 < data.size()) {
                    uint8_t b0 = data[idx0];
                    uint8_t b1 = (idx0 + 1 < data.size()) ? data[idx0 + 1] : b0;

                    // 8-bit unsigned PCM to normalized float [-1.0f, 1.0f]
                    float s0 = (static_cast<float>(b0) - 128.0f) / 128.0f;
                    float s1 = (static_cast<float>(b1) - 128.0f) / 128.0f;
                    float sample = static_cast<float>((1.0 - frac) * static_cast<double>(s0) + frac * static_cast<double>(s1));

                    mix_l += sample * ch.vol_left;
                    mix_r += sample * ch.vol_right;

                    ch.cursor += ch.rate_step;
                    if (ch.cursor >= static_cast<double>(data.size())) {
                        if (ch.loop) {
                            ch.cursor -= static_cast<double>(data.size());
                        } else {
                            ch.active = false;
                        }
                    }
                } else {
                    ch.active = false;
                }
            }
            mix_l *= sfx_gain;
            mix_r *= sfx_gain;
        }

        if (has_music && music_buf) {
            if (music_stream_->decoder.channels == 2) {
                mix_l += music_buf[f * 2 + 0] * music_gain;
                mix_r += music_buf[f * 2 + 1] * music_gain;
            } else {
                mix_l += music_buf[f] * music_gain;
                mix_r += music_buf[f] * music_gain;
            }
        }

        mix_l = std::clamp(mix_l, -1.0f, 1.0f);
        mix_r = std::clamp(mix_r, -1.0f, 1.0f);

        out_stereo[f * 2 + 0] = static_cast<int16_t>(mix_l * 32767.0f);
        out_stereo[f * 2 + 1] = static_cast<int16_t>(mix_r * 32767.0f);
    }
}


std::vector<int16_t> AudioMixer::render_frames(size_t num_frames) {
    std::vector<int16_t> buffer(num_frames * 2, 0);
    mix_samples_i16(buffer.data(), num_frames);
    return buffer;
}

} // namespace ants::app
