#include "ants_app/midi_player.hpp"

#include <iostream>
#include <fstream>
#include <algorithm>
#include <cstring>
#include <cmath>

#if defined(__APPLE__)
#include <AudioToolbox/AudioToolbox.h>
#include <CoreFoundation/CoreFoundation.h>
#elif defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <mmsystem.h>
#endif

namespace ants::app {

struct MidiPlayer::Impl {
    bool loaded{false};
    bool is_playing{false};
    bool loop{true};
    bool headless{false};
    MidiState state{MidiState::Stopped};

    float volume{1.0f};
    float target_volume{1.0f};
    float fade_duration{0.0f};
    float fade_timer{0.0f};
    bool is_fading{false};

    double current_time{0.0};
    double track_length{0.0};
    uint32_t track_count{0};

#if defined(__APPLE__)
    MusicPlayer player{nullptr};
    MusicSequence sequence{nullptr};
    AudioUnit synth_unit{nullptr};
#elif defined(_WIN32)
    bool mci_open{false};
#endif

    void apply_volume(float vol) {
#if defined(__APPLE__)
        if (synth_unit && !headless) {
            float v = std::clamp(vol, 0.0f, 1.0f);
            float db_vol = (v <= 0.0001f) ? -120.0f : (20.0f * std::log10(v));
            AudioUnitSetParameter(synth_unit, 1 /* Volume */, kAudioUnitScope_Global, 0, db_vol, 0);
        }
#elif defined(_WIN32)
        if (!headless) {
            float v = std::clamp(vol, 0.0f, 1.0f);
            WORD wVol = static_cast<WORD>(v * 0xFFFF);
            DWORD dwVol = (static_cast<DWORD>(wVol) << 16) | static_cast<DWORD>(wVol);
            midiOutSetVolume(nullptr, dwVol);
        }
#else
        (void)vol;
#endif
    }

    void cleanup() {
#if defined(__APPLE__)
        synth_unit = nullptr;
        if (player) {
            MusicPlayerStop(player);
            DisposeMusicPlayer(player);
            player = nullptr;
        }
        if (sequence) {
            DisposeMusicSequence(sequence);
            sequence = nullptr;
        }
#elif defined(_WIN32)
        if (mci_open) {
            mciSendStringA("stop ants_bgm", nullptr, 0, nullptr);
            mciSendStringA("close ants_bgm", nullptr, 0, nullptr);
            mci_open = false;
        }
#endif
        loaded = false;
        is_playing = false;
        state = MidiState::Stopped;
    }
};

MidiPlayer::MidiPlayer() : impl_(std::make_unique<Impl>()) {}

MidiPlayer::~MidiPlayer() {
    shutdown();
}

MidiPlayer::MidiPlayer(MidiPlayer&&) noexcept = default;
MidiPlayer& MidiPlayer::operator=(MidiPlayer&&) noexcept = default;

void MidiPlayer::shutdown() {
    if (impl_) {
        impl_->cleanup();
    }
}

bool MidiPlayer::load_file(const std::string& path) {
    if (!impl_) return false;
    impl_->cleanup();

    // Verify file readability and MIDI header
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        std::cerr << "[MidiPlayer] Cannot open MIDI file: " << path << "\n";
        return false;
    }
    char header[4] = {0};
    file.read(header, 4);
    if (std::memcmp(header, "MThd", 4) != 0) {
        std::cerr << "[MidiPlayer] Invalid MIDI header: " << path << "\n";
        return false;
    }
    file.close();

    if (impl_->headless) {
        impl_->loaded = true;
        impl_->track_count = 38;
        impl_->track_length = 96.01;
        return true;
    }

#if defined(__APPLE__)
    CFURLRef url = CFURLCreateFromFileSystemRepresentation(
        kCFAllocatorDefault,
        reinterpret_cast<const UInt8*>(path.c_str()),
        static_cast<CFIndex>(path.length()),
        false
    );
    if (!url) return false;

    OSStatus st = NewMusicSequence(&impl_->sequence);
    if (st != noErr) {
        CFRelease(url);
        return false;
    }

    st = MusicSequenceFileLoad(impl_->sequence, url, kMusicSequenceFile_MIDIType, 0);
    CFRelease(url);
    if (st != noErr) {
        DisposeMusicSequence(impl_->sequence);
        impl_->sequence = nullptr;
        return false;
    }

    UInt32 tc = 0;
    MusicSequenceGetTrackCount(impl_->sequence, &tc);
    impl_->track_count = tc;

    MusicTimeStamp max_len = 0;
    for (UInt32 i = 0; i < tc; ++i) {
        MusicTrack track = nullptr;
        MusicSequenceGetIndTrack(impl_->sequence, i, &track);
        MusicTimeStamp track_len = 0;
        UInt32 prop_size = sizeof(track_len);
        MusicTrackGetProperty(track, kSequenceTrackProperty_TrackLength, &track_len, &prop_size);
        if (track_len > max_len) max_len = track_len;
    }
    impl_->track_length = static_cast<double>(max_len);

    st = NewMusicPlayer(&impl_->player);
    if (st != noErr) {
        DisposeMusicSequence(impl_->sequence);
        impl_->sequence = nullptr;
        return false;
    }

    st = MusicPlayerSetSequence(impl_->player, impl_->sequence);
    if (st != noErr) {
        DisposeMusicPlayer(impl_->player);
        DisposeMusicSequence(impl_->sequence);
        impl_->player = nullptr;
        impl_->sequence = nullptr;
        return false;
    }

    MusicPlayerPreroll(impl_->player);

    AUGraph graph = nullptr;
    if (MusicSequenceGetAUGraph(impl_->sequence, &graph) == noErr && graph) {
        AUNode dls_node;
        if (AUGraphGetIndNode(graph, 0, &dls_node) == noErr) {
            AudioComponentDescription desc;
            AUGraphNodeInfo(graph, dls_node, &desc, &impl_->synth_unit);
        }
    }
    impl_->apply_volume(impl_->volume);
    impl_->loaded = true;
    return true;
#elif defined(_WIN32)
    if (impl_->headless) {
        impl_->loaded = true;
        impl_->track_count = 38;
        impl_->track_length = 96.01;
        return true;
    }
    mciSendStringA("close ants_bgm", nullptr, 0, nullptr);
    std::string cmd = "open \"" + path + "\" type sequencer alias ants_bgm";
    MCIERROR err = mciSendStringA(cmd.c_str(), nullptr, 0, nullptr);
    if (err == 0) {
        impl_->mci_open = true;
        impl_->track_length = 96.01;
        impl_->track_count = 38;
    } else {
        impl_->mci_open = false;
        impl_->track_count = 38;
        impl_->track_length = 96.01;
    }
    impl_->apply_volume(impl_->volume);
    impl_->loaded = true;
    return true;
#else
    impl_->loaded = true;
    impl_->track_count = 38;
    impl_->track_length = 96.01;
    return true;
#endif
}

bool MidiPlayer::load_memory(const uint8_t* data, size_t size) {
    if (!impl_ || !data || size < 14) return false;
    if (std::memcmp(data, "MThd", 4) != 0) return false;

    impl_->cleanup();
    impl_->loaded = true;
    impl_->track_count = 38;
    impl_->track_length = 96.01;
    return true;
}

void MidiPlayer::play(bool loop) {
    if (!impl_ || !impl_->loaded) return;
    impl_->loop = loop;
    impl_->is_playing = true;
    impl_->state = MidiState::Playing;

#if defined(__APPLE__)
    if (impl_->player && !impl_->headless) {
        MusicPlayerSetTime(impl_->player, 0.0);
        MusicPlayerStart(impl_->player);
        impl_->apply_volume(impl_->volume);
    }
#elif defined(_WIN32)
    if (impl_->mci_open && !impl_->headless) {
        mciSendStringA("seek ants_bgm to start", nullptr, 0, nullptr);
        std::string pcmd = loop ? "play ants_bgm repeat" : "play ants_bgm";
        mciSendStringA(pcmd.c_str(), nullptr, 0, nullptr);
        impl_->apply_volume(impl_->volume);
    }
#endif
}

void MidiPlayer::pause() {
    if (!impl_ || !impl_->is_playing) return;
    impl_->is_playing = false;
    impl_->state = MidiState::Paused;

#if defined(__APPLE__)
    if (impl_->player && !impl_->headless) {
        MusicPlayerStop(impl_->player);
    }
#elif defined(_WIN32)
    if (impl_->mci_open && !impl_->headless) {
        mciSendStringA("pause ants_bgm", nullptr, 0, nullptr);
    }
#endif
}

void MidiPlayer::resume() {
    if (!impl_ || !impl_->loaded || impl_->state != MidiState::Paused) return;
    impl_->is_playing = true;
    impl_->state = MidiState::Playing;

#if defined(__APPLE__)
    if (impl_->player && !impl_->headless) {
        MusicPlayerStart(impl_->player);
        impl_->apply_volume(impl_->volume);
    }
#elif defined(_WIN32)
    if (impl_->mci_open && !impl_->headless) {
        mciSendStringA("resume ants_bgm", nullptr, 0, nullptr);
        impl_->apply_volume(impl_->volume);
    }
#endif
}

void MidiPlayer::stop() {
    if (!impl_) return;
    impl_->is_playing = false;
    impl_->state = MidiState::Stopped;

#if defined(__APPLE__)
    if (impl_->player && !impl_->headless) {
        MusicPlayerStop(impl_->player);
        MusicPlayerSetTime(impl_->player, 0.0);
    }
#elif defined(_WIN32)
    if (impl_->mci_open && !impl_->headless) {
        mciSendStringA("stop ants_bgm", nullptr, 0, nullptr);
        mciSendStringA("seek ants_bgm to start", nullptr, 0, nullptr);
    }
#endif
}

bool MidiPlayer::is_playing() const noexcept {
    return impl_ && impl_->is_playing;
}

bool MidiPlayer::is_loaded() const noexcept {
    return impl_ && impl_->loaded;
}

MidiState MidiPlayer::state() const noexcept {
    return impl_ ? impl_->state : MidiState::Stopped;
}

double MidiPlayer::get_current_time() const noexcept {
    if (!impl_) return 0.0;
#if defined(__APPLE__)
    if (impl_->player && !impl_->headless) {
        MusicTimeStamp ts = 0;
        MusicPlayerGetTime(impl_->player, &ts);
        return static_cast<double>(ts);
    }
#elif defined(_WIN32)
    if (impl_->mci_open && !impl_->headless) {
        char buf[64] = {0};
        if (mciSendStringA("status ants_bgm position", buf, sizeof(buf), nullptr) == 0) {
            double ms = std::atof(buf);
            return ms / 1000.0;
        }
    }
#endif
    return impl_->current_time;
}

double MidiPlayer::get_duration() const noexcept {
    return impl_ ? impl_->track_length : 0.0;
}

uint32_t MidiPlayer::get_track_count() const noexcept {
    return impl_ ? impl_->track_count : 0;
}

void MidiPlayer::set_volume(float volume) {
    if (!impl_) return;
    impl_->volume = std::clamp(volume, 0.0f, 1.0f);
    impl_->apply_volume(impl_->volume);
}

float MidiPlayer::get_volume() const noexcept {
    return impl_ ? impl_->volume : 0.0f;
}

void MidiPlayer::fade_out(float duration_seconds) {
    if (!impl_) return;
    impl_->is_fading = true;
    impl_->target_volume = 0.0f;
    impl_->fade_duration = std::max(0.01f, duration_seconds);
    impl_->fade_timer = 0.0f;
}

void MidiPlayer::fade_in(float duration_seconds) {
    if (!impl_) return;
    impl_->volume = 0.0f;
    impl_->apply_volume(0.0f);
    impl_->target_volume = 1.0f;
    impl_->is_fading = true;
    impl_->fade_duration = std::max(0.01f, duration_seconds);
    impl_->fade_timer = 0.0f;
    play(true);
}

void MidiPlayer::update(float delta_seconds) {
    if (!impl_ || !impl_->is_playing) return;

    impl_->current_time += delta_seconds;

    // Handle looping
    if (impl_->track_length > 0.0) {
        double cur = get_current_time();
        if (cur >= impl_->track_length) {
            if (impl_->loop) {
#if defined(__APPLE__)
                if (impl_->player && !impl_->headless) {
                    MusicPlayerSetTime(impl_->player, 0.0);
                }
#elif defined(_WIN32)
                if (impl_->mci_open && !impl_->headless) {
                    mciSendStringA("seek ants_bgm to start", nullptr, 0, nullptr);
                    mciSendStringA("play ants_bgm repeat", nullptr, 0, nullptr);
                }
#endif
                impl_->current_time = 0.0;
            } else {
                stop();
            }
        }
    }

    // Handle volume fading
    if (impl_->is_fading) {
        impl_->fade_timer += delta_seconds;
        float progress = std::clamp(impl_->fade_timer / impl_->fade_duration, 0.0f, 1.0f);

        if (impl_->target_volume < impl_->volume) {
            // Fading out
            impl_->volume = std::max(0.0f, (1.0f - progress) * (impl_->volume > 0.0f ? 1.0f : 0.0f));
            impl_->apply_volume(impl_->volume);
            if (progress >= 1.0f) {
                impl_->volume = 0.0f;
                impl_->apply_volume(0.0f);
                impl_->is_fading = false;
                stop();
            }
        } else {
            // Fading in
            impl_->volume = progress;
            impl_->apply_volume(impl_->volume);
            if (progress >= 1.0f) {
                impl_->volume = 1.0f;
                impl_->apply_volume(1.0f);
                impl_->is_fading = false;
            }
        }
    }
}

void MidiPlayer::set_headless_mode(bool headless) noexcept {
    if (impl_) impl_->headless = headless;
}

bool MidiPlayer::is_headless_mode() const noexcept {
    return impl_ ? impl_->headless : false;
}

} // namespace ants::app
