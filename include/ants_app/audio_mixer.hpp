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
constexpr int32_t AUDIO_LISTENER_RADIUS = 2500;     // world pixels: the radius of the map view's sound container (0x102f5cb), the span of the law below

/**
 * @brief Represents an active playback channel in the software mixer.
 */
struct MixerChannel {
    bool active{false};
    const ants::assets::SoundClip* clip{nullptr};
    double cursor{0.0};       // Fractional sample index in clip
    double rate_step{1.0};    // clip_rate / mixer_rate
    float vol_left{1.0f};     // 0.0 .. 1.0, the dB law of the original applied (see AudioMixer::apply_law)
    float vol_right{1.0f};    // 0.0 .. 1.0
    float event_gain{1.0f};   // the `volume` argument of play_sfx / play_spatial (1.0 for everything the game plays)
    uint32_t owner{0};        // the sprite that started the sound (tracked sounds): stop_owner() cuts it; 0 = nobody
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
 * resamples with linear interpolation, applies the original's sound law (distance percent, pan and the
 * Sound Volume option as DirectSound attenuations in hundredths of a dB, Ants.exe 0x102e8e4 / 0x102d803),
 * priority preemption, and outputs 44100 Hz 16-bit stereo PCM.
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
    int play_sfx(uint32_t sound_id, float volume = 1.0f, uint8_t priority = 128, bool loop = false, uint32_t owner = 0);
    int play_spatial(uint32_t sound_id, int32_t world_x, int32_t world_y, uint8_t priority = 128, float volume = 1.0f, bool loop = false, uint32_t owner = 0);
    void stop_all();
    /// StopTracked (FUN_0102bdab): every sound that `owner` started and that still plays is cut (the owner's clip was replaced, or the owner was removed)
    void stop_owner(uint32_t owner);

    // Queries
    bool is_channel_active(int channel_id) const;
    size_t active_channel_count() const;

    // Listener / Spatial Positioning
    void set_listener_position(int32_t world_x, int32_t world_y);
    int32_t listener_x() const noexcept { return listener_x_; }
    int32_t listener_y() const noexcept { return listener_y_; }
    /// The gains of a source at a world position for the current listener and Sound Volume (the dB law below), times `base_vol`.
    void calculate_spatial_pan(int32_t world_x, int32_t world_y, float base_vol, float& out_vol_l, float& out_vol_r) const;

    // The original's sound law. Distances are world pixels; everything is integer arithmetic as in the binary.
    /// FUN_0102e8e4: the volume percent of a source (dx, dy) away from the listener: 100 - trunc(max(|dx|, |dy|) * 100 / 2500) (Chebyshev), 0 beyond the radius
    static int32_t distance_percent(int32_t dx, int32_t dy) noexcept;
    /// FUN_0102e8e4: the pan in hundredths of a dB, 25 * trunc(dx * 100 / 2500); positive = the source is right of the listener and the LEFT channel is attenuated by it
    static int32_t pan_centibels(int32_t dx) noexcept;
    /// FUN_0102d803: the DirectSound volume in hundredths of a dB: 25 * ((SV * pct / 100) - 100), SV = the Sound Volume option 0..100; SV 0 = -10000 (mute)
    static int32_t attenuation_centibels(int32_t sound_volume, int32_t pct) noexcept;
    /// DirectSound: amplitude = 10^(centibels / 2000); -10000 and below is silence
    static float gain_from_centibels(int32_t centibels) noexcept;

    // Volume Controls. The Sound Volume option is an integer 0..100 (default 100) that enters the law of EVERY sound (non-positional ones too) and is applied to the
    // sounds that are playing (FUN_0102f777); the game sets it when the options slider is released.
    void set_sound_volume(int32_t sound_volume);
    int32_t sound_volume() const noexcept { return sound_volume_; }
    void set_sfx_volume(float volume);          // volume 0.0 .. 1.0 -> Sound Volume 0 .. 100
    /// The gains of a playing channel (inspection)
    bool channel_volumes(int channel_id, float& left, float& right) const;

    // Simulation Audio Ingestion
    void ingest_simulation_events(const std::vector<ants::sim::AudioEvent>& events, uint8_t local_player_id = 0);

    // Audio Rendering / DSP
    void mix_samples_i16(int16_t* out_stereo, size_t num_frames);
    std::vector<int16_t> render_frames(size_t num_frames); // Headless testing helper

    // Background Music (Streaming MP3)
    bool play_music(const std::string& filepath, bool loop = true);
    void stop_music();
    void pause_music();
    void resume_music();
    void set_music_volume(float volume);
    float get_music_volume() const noexcept;
    bool is_music_playing() const;
    /// The file that the music stream was started with ("" when nothing was), and whether it loops (inspection)
    std::string music_filepath() const;
    bool music_loops() const;
    void fade_out_music(float duration_sec);
    void update_music(float dt);

    // Headless / Mock Mode
    void set_headless_mode(bool headless) noexcept { headless_mode_ = headless; }

private:
    int allocate_channel(uint8_t priority);
    void apply_law(MixerChannel& ch) const;       // the caller holds the mutex

    const ants::assets::AssetArchive* archive_{nullptr};
    uint32_t output_sample_rate_{AUDIO_DEFAULT_SAMPLE_RATE};
    std::array<MixerChannel, AUDIO_MIXER_MAX_CHANNELS> channels_{};

    int32_t listener_x_{0};
    int32_t listener_y_{0};

    float master_volume_{1.0f};
    int32_t sound_volume_{100};                   // the Sound Volume option, 0 .. 100

    bool headless_mode_{false};
    uint32_t sdl_audio_device_{0};

    struct MusicStream;
    std::unique_ptr<MusicStream> music_stream_;

    mutable std::mutex mixer_mutex_;
};

} // namespace ants::app
