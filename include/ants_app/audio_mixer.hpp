#pragma once

#include <cstdint>
#include <cstddef>
#include <string>
#include <vector>
#include <array>
#include <mutex>
#include <memory>

#include "ants_assets/asset_archive.hpp"
#include "ants_sim/sim_engine.hpp"

namespace ants::app {

constexpr size_t AUDIO_MIXER_MAX_CHANNELS = 32;
constexpr uint32_t AUDIO_DEFAULT_SAMPLE_RATE = 44100;
constexpr float AUDIO_MAX_AUDIBLE_DISTANCE = 800.0f; // World pixels

/**
 * @brief Represents an active playback channel in the software mixer.
 */
struct MixerChannel {
    bool active{false};
    uint32_t sound_id{0};
    const ants::assets::SoundClip* clip{nullptr};
    double cursor{0.0};       // Fractional sample index in clip
    double rate_step{1.0};    // clip_rate / mixer_rate
    float vol_left{1.0f};     // 0.0 .. 1.0
    float vol_right{1.0f};    // 0.0 .. 1.0
    uint8_t priority{0};      // 0..255 (255 = critical/uninterruptible)
    bool loop{false};
    bool spatial{false};
    int32_t world_x{0};
    int32_t world_y{0};
};

/**
 * @brief 32-channel real-time software PCM audio mixer.
 * 
 * Ingests 8-bit unsigned mono PCM clips (11025/22050 Hz) from AssetArchive,
 * resamples with linear interpolation, computes equal-power spatial panning,
 * applies priority preemption, and outputs 44100 Hz 16-bit stereo PCM.
 */
class AudioMixer {
public:
    AudioMixer();
    ~AudioMixer();

    AudioMixer(const AudioMixer&) = delete;
    AudioMixer& operator=(const AudioMixer&) = delete;
    AudioMixer(AudioMixer&&) noexcept;
    AudioMixer& operator=(AudioMixer&&) noexcept;

    // Initialization
    bool init(const ants::assets::AssetArchive& archive, uint32_t output_sample_rate = AUDIO_DEFAULT_SAMPLE_RATE);
    bool init_sdl_audio(uint16_t buffer_samples = 1024);
    void shutdown_sdl_audio();

    // Playback Controls
    int play_sfx(uint32_t sound_id, float volume = 1.0f, uint8_t priority = 128, bool loop = false);
    int play_spatial(uint32_t sound_id, int32_t world_x, int32_t world_y, uint8_t priority = 128, float volume = 1.0f, bool loop = false);
    void stop_sound(int channel_id);
    void stop_all();

    // Queries
    bool is_channel_active(int channel_id) const;
    size_t active_channel_count() const;
    const MixerChannel& get_channel(size_t index) const { return channels_[index]; }

    // Listener / Spatial Positioning
    void set_listener_position(int32_t world_x, int32_t world_y);
    void get_listener_position(int32_t& world_x, int32_t& world_y) const;
    void calculate_spatial_pan(int32_t world_x, int32_t world_y, float base_vol, float& out_vol_l, float& out_vol_r) const;

    // Volume Controls (0.0f .. 1.0f)
    void set_master_volume(float volume);
    void set_sfx_volume(float volume);
    float get_master_volume() const noexcept { return master_volume_; }
    float get_sfx_volume() const noexcept { return sfx_volume_; }

    // Simulation Audio Ingestion
    void ingest_simulation_events(const std::vector<ants::sim::AudioEvent>& events, uint8_t local_player_id = 0);

    // Audio Rendering / DSP
    void mix_samples_i16(int16_t* out_stereo, size_t num_frames);
    void mix_samples_f32(float* out_stereo, size_t num_frames);
    std::vector<int16_t> render_frames(size_t num_frames); // Headless testing helper

    // Headless / Mock Mode
    void set_headless_mode(bool headless) noexcept { headless_mode_ = headless; }
    bool is_headless_mode() const noexcept { return headless_mode_; }

private:
    int allocate_channel(uint8_t priority);

    const ants::assets::AssetArchive* archive_{nullptr};
    uint32_t output_sample_rate_{AUDIO_DEFAULT_SAMPLE_RATE};
    std::array<MixerChannel, AUDIO_MIXER_MAX_CHANNELS> channels_{};

    int32_t listener_x_{0};
    int32_t listener_y_{0};

    float master_volume_{1.0f};
    float sfx_volume_{1.0f};

    bool headless_mode_{false};
    uint32_t sdl_audio_device_{0};

    mutable std::mutex mixer_mutex_;
};

} // namespace ants::app
