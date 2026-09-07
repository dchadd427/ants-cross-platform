#pragma once

#include <string>
#include <memory>
#include <cstdint>
#include <cstddef>

namespace ants::app {

enum class MidiState {
    Stopped,
    Playing,
    Paused
};

/**
 * @brief Native macOS AudioToolbox MIDI background music player.
 * 
 * Plays standard MIDI files (e.g. INTRO.MID) using macOS AudioToolbox MusicPlayer
 * and Apple DLS General MIDI Synthesizer without third-party dependencies.
 * Supports looping, volume attenuation, and smooth fading.
 */
class MidiPlayer {
public:
    MidiPlayer();
    ~MidiPlayer();

    MidiPlayer(const MidiPlayer&) = delete;
    MidiPlayer& operator=(const MidiPlayer&) = delete;
    MidiPlayer(MidiPlayer&&) noexcept;
    MidiPlayer& operator=(MidiPlayer&&) noexcept;

    // Loading
    bool load_file(const std::string& path);
    bool load_memory(const uint8_t* data, size_t size);

    // Playback Lifecycle
    void play(bool loop = true);
    void pause();
    void resume();
    void stop();

    // State Inspection
    bool is_playing() const noexcept;
    bool is_loaded() const noexcept;
    MidiState state() const noexcept;
    double get_current_time() const noexcept;
    double get_duration() const noexcept;
    uint32_t get_track_count() const noexcept;

    // Volume & Fading
    void set_volume(float volume); // 0.0f .. 1.0f
    float get_volume() const noexcept;
    void fade_out(float duration_seconds);
    void fade_in(float duration_seconds);
    void update(float delta_seconds);

    // Headless / Testing Configuration
    void set_headless_mode(bool headless) noexcept;
    bool is_headless_mode() const noexcept;

    void shutdown();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace ants::app
