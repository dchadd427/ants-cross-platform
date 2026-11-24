#pragma once

#include <cstdint>
#include <cstddef>
#include <string>
#include <vector>
#include <array>
#include <atomic>
#include <mutex>
#include <memory>

#include "ants_assets/asset_archive.hpp"
#include "ants_sim/sim_engine.hpp"

namespace ants::app {

constexpr size_t AUDIO_MIXER_MAX_CHANNELS = 32;
constexpr size_t AUDIO_MIXER_MAX_RELEASE_TAILS = 32;
constexpr uint32_t AUDIO_DEFAULT_SAMPLE_RATE = 44100;
constexpr int32_t AUDIO_LISTENER_RADIUS = 2500;     // world pixels: the radius of the map view's sound container (0x102f5cb), the span of the law below

/// A gain that the law changes while its sound plays (the view moves or zooms: the listener is the middle of the view) goes there in a ramp of this length for a change of the full range 0 .. 1
/// A step in the gain of a playing sound is a click, and a faint sound that gets loud in one sample is heard as played again.
constexpr float AUDIO_GAIN_RAMP_MS = 12.0f;

/// Short fade-out tail on stop_owner(), stop_all() (live playback), and voice preemption so cut waveforms never pop.
constexpr float AUDIO_RELEASE_TAIL_MS = 4.0f;

/// Soft-knee master bus limiter threshold (strictly linear y = x for |x| <= threshold, smooth compression toward 1.0 above).
constexpr float AUDIO_LIMITER_KNEE = 0.85f;

/// Safe makeup gain (+6 dB) for the quiet pre-rendered MP3 soundtrack (peaks remain below AUDIO_LIMITER_KNEE at 100% music volume).
constexpr float AUDIO_MUSIC_MAKEUP_GAIN = 2.0f;

/**
 * @brief Pre-decoded float32 PCM clip (mono or interleaved stereo in [-1.0, 1.0]).
 */
struct DecodedClip {
    std::vector<float> samples;
    uint32_t sample_rate{0};
    uint16_t channels{1};

    size_t frame_count() const noexcept {
        return channels > 0 ? (samples.size() / static_cast<size_t>(channels)) : 0u;
    }
    bool empty() const noexcept { return frame_count() == 0; }
};

/**
 * @brief Represents an active playback channel in the software mixer.
 */
struct MixerChannel {
    bool active{false};
    std::shared_ptr<const DecodedClip> clip;
    double cursor{0.0};       // Fractional frame index in clip
    double rate_step{1.0};    // clip_rate / mixer_rate
    bool lpf_enabled{false};  // Source-Nyquist 1-pole anti-imaging low-pass filter active when clip_rate < output_rate
    float lpf_alpha{1.0f};
    float lpf_l{0.0f};
    float lpf_r{0.0f};
    float vol_left{1.0f};     // 0.0 .. 1.0, the dB law of the original applied (see AudioMixer::apply_law): where the gain is going
    float vol_right{1.0f};    // 0.0 .. 1.0
    float mix_left{1.0f};     // the gains that are mixed now: the sound's start is at its law at once; a move of the listener takes AUDIO_GAIN_RAMP_MS to get there (no step in the wave)
    float mix_right{1.0f};
    float event_gain{1.0f};   // the `volume` argument of play_sfx / play_spatial (1.0 for everything the game plays, damped for same-tick duplicates)
    uint32_t owner{0};        // the sprite that started the sound (tracked sounds): stop_owner() cuts it; 0 = nobody
    uint8_t priority{0};      // 0..255 (255 = critical/uninterruptible)
    bool loop{false};
    bool spatial{false};
    int32_t world_x{0};
    int32_t world_y{0};
};

/**
 * @brief Bounded de-click release tail for voices cut by stop_owner(), stop_all() (live), or preemption.
 */
struct ReleaseTail {
    bool active{false};
    std::shared_ptr<const DecodedClip> clip;
    double cursor{0.0};
    double rate_step{1.0};
    bool loop{false};
    bool lpf_enabled{false};
    float lpf_alpha{1.0f};
    float lpf_l{0.0f};
    float lpf_r{0.0f};
    float mix_left{0.0f};
    float mix_right{0.0f};
    float fade_gain{0.0f};
    float fade_step{0.0f};
};

/**
 * @brief 32-channel real-time software PCM audio mixer.
 *
 * Pre-decodes 8-bit/16-bit mono/stereo PCM clips from AssetArchive into float32 (with DC-offset removal
 * and edge anti-click tapering), resamples with 4-point Hermite interpolation + source-Nyquist low-pass
 * anti-imaging filtering, applies the original's sound law (distance percent, pan and the Sound Volume
 * option as DirectSound attenuations in hundredths of a dB, Ants.exe 0x102e8e4 / 0x102d803), priority
 * preemption with de-click release tails, soft-knee master bus limiting, and outputs 44100 Hz 16-bit stereo PCM.
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

    // Clip Decoding & Optional Sound Pack Overrides (float32 mono or interleaved stereo in [-1.0, 1.0])
    static DecodedClip decode_sound_clip(const ants::assets::SoundClip& clip);
    bool set_clip_override(uint32_t sound_id, std::vector<float> samples, uint32_t sample_rate, uint16_t channels = 1);
    void clear_clip_override(uint32_t sound_id);
    void clear_clip_overrides();
    bool has_clip_override(uint32_t sound_id) const;
    std::shared_ptr<const DecodedClip> active_clip(uint32_t sound_id) const;

    // Playback Controls
    int play_sfx(uint32_t sound_id, float volume = 1.0f, uint8_t priority = 128, bool loop = false, uint32_t owner = 0);
    int play_spatial(uint32_t sound_id, int32_t world_x, int32_t world_y, uint8_t priority = 128, float volume = 1.0f, bool loop = false, uint32_t owner = 0);
    /// Stops all active channels. In live mode (!headless) active voices fade out over AUDIO_RELEASE_TAIL_MS to prevent clicks; in headless mode channels and tails reset immediately for deterministic test harnesses.
    void stop_all();
    /// StopTracked (FUN_0102bdab): every sound that `owner` started and that still plays is cut with a de-click release tail (active_channel_count drops immediately)
    void stop_owner(uint32_t owner);

    // Queries
    bool is_channel_active(int channel_id) const;
    size_t active_channel_count() const;
    size_t active_release_tail_count() const;

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

    // DSP & Presentation Helpers
    /// Soft-knee master limiter: strictly linear y = x for |x| <= AUDIO_LIMITER_KNEE (0.85), C1-smooth tanh compression toward 1.0 above.
    static float soft_limit_sample(float x) noexcept;
    /// Presentation-side voice priority tier for simulation events (alarms/countdowns > explosions/scores > combat/abilities > normal > bumps).
    static uint8_t sim_event_priority(uint32_t sound_id, uint8_t base_priority) noexcept;

    // Volume Controls. The Sound Volume option is an integer 0..100 (default 100) that enters the law of EVERY sound (non-positional ones too) and is applied to the
    // sounds that are playing (FUN_0102f777); the game sets it when the options slider is released.
    void set_sound_volume(int32_t sound_volume);
    int32_t sound_volume() const noexcept { return sound_volume_; }
    void set_sfx_volume(float volume);          // volume 0.0 .. 1.0 -> Sound Volume 0 .. 100
    /// The settings menu's Master volume and Mute (game_menu.hpp): one gain, 0.0 .. 1.0, on everything that is heard (sounds and music). It is reached by a short ramp, so that a slider that is dragged makes no click.
    /// The Sound Volume and the Music Volume stay as they are; 1.0 (the start) changes nothing of the mix.
    void set_output_gain(float gain);
    float output_gain() const;                  // where it is set to (not where its ramp is)
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
    void set_headless_mode(bool headless) noexcept { headless_mode_.store(headless, std::memory_order_relaxed); }

    /// Inspection (tests): every sound that is given a channel is appended to `log` (null: none). A sound that is dropped (no channel, volume 0) is not a start.
    struct SoundStart {
        uint32_t sound_id{0};
        uint32_t owner{0};
        int32_t world_x{0};
        int32_t world_y{0};
        bool spatial{false};
        uint8_t priority{0};
    };
    void set_start_log(std::vector<SoundStart>* log) noexcept { start_log_ = log; }

private:
    int allocate_channel(uint8_t priority);
    void spawn_release_tail(const MixerChannel& ch);                 // caller holds mixer_mutex_
    void mix_chunk_i16_locked(int16_t* out_stereo, size_t chunk_frames); // caller holds mixer_mutex_
    std::shared_ptr<const DecodedClip> resolve_clip_locked(uint32_t sound_id) const; // caller holds mixer_mutex_
    void apply_law(MixerChannel& ch, bool snap = true) const;       // caller holds mixer_mutex_; `snap`: mixed gains take the law at once, else they ramp there

    const ants::assets::AssetArchive* archive_{nullptr};
    uint32_t output_sample_rate_{AUDIO_DEFAULT_SAMPLE_RATE};
    std::vector<std::shared_ptr<const DecodedClip>> base_clips_;
    std::vector<std::shared_ptr<const DecodedClip>> override_clips_;
    std::array<MixerChannel, AUDIO_MIXER_MAX_CHANNELS> channels_{};
    std::array<ReleaseTail, AUDIO_MIXER_MAX_RELEASE_TAILS> release_tails_{};

    std::vector<SoundStart>* start_log_{nullptr};  // (inspection; the caller holds the mutex when it is written)
    int32_t listener_x_{0};
    int32_t listener_y_{0};

    float output_gain_target_{1.0f};              // set_output_gain
    float output_gain_now_{1.0f};                 // where the ramp to it is (the mixing thread)
    int32_t sound_volume_{100};                   // the Sound Volume option, 0 .. 100

    std::atomic<bool> headless_mode_{false};
    uint32_t sdl_audio_device_{0};

    struct MusicStream;
    std::unique_ptr<MusicStream> music_stream_;

    mutable std::mutex mixer_mutex_;
};

} // namespace ants::app
