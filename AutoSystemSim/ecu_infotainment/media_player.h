// AutoSystemSim/ecu_infotainment/media_player.h
#ifndef MEDIA_PLAYER_H
#define MEDIA_PLAYER_H

#include "../common/logger.h"
#include "../common/datatypes.h" // May not be directly used, but good practice to include if common types might be relevant
#include <string>
#include <vector>
#include <chrono> // For track duration/elapsed time

namespace ecu_infotainment {

enum class MediaSource {
    USB,
    BLUETOOTH,
    RADIO_FM,
    RADIO_AM,
    AUX,
    NONE // No active source
};

enum class PlaybackStatus {
    STOPPED,
    PLAYING,
    PAUSED,
    BUFFERING, // e.g., for streaming, though we'll simplify
    ERROR_SOURCE_UNAVAILABLE,
    ERROR_TRACK_UNREADABLE
};

struct TrackInfo {
    std::string title;
    std::string artist;
    std::string album;
    std::chrono::seconds duration;
    int track_number; // In playlist or album
};

class MediaPlayer {
public:
    MediaPlayer();
    ~MediaPlayer();

    // --- Source Selection ---
    bool selectSource(MediaSource new_source);
    MediaSource getCurrentSource() const;

    // --- Playback Controls ---
    bool play();
    bool pause();
    bool stop();
    bool nextTrack();
    bool previousTrack();
    bool seek(std::chrono::seconds position); // Seek within the current track

    // --- Volume Control ---
    bool setVolume(int level_percent); // 0-100%
    int getVolume() const;
    bool mute(bool enable_mute);
    bool isMuted() const;

    // --- Status ---
    PlaybackStatus getPlaybackStatus() const;
    TrackInfo getCurrentTrackInfo() const;
    std::chrono::seconds getCurrentTrackElapsedTime() const;

    // --- Playlist/Radio (Simplified) ---
    bool loadPlaylist(const std::vector<TrackInfo>& playlist); // For USB/Bluetooth
    bool tuneRadio(double frequency, MediaSource radio_band); // For RADIO_FM/AM

    // --- Periodic Update ---
    void updatePlaybackState(); // Simulates time passing, track ending, etc.

private:
    MediaSource current_source_;
    PlaybackStatus current_status_;
    int volume_level_; // 0-100
    bool muted_;

    // Current media details
    std::vector<TrackInfo> current_playlist_;
    int current_track_index_;
    std::chrono::steady_clock::time_point track_start_time_;
    std::chrono::seconds paused_elapsed_time_; // Time elapsed when pause was hit

    // Radio details
    double current_fm_frequency_;
    double current_am_frequency_;

    // Internal helpers
    bool playTrackAtIndex(int index);
    void simulateTimePassing();
    void handleTrackEnd();
    void reportPlaybackError(PlaybackStatus error_status, const std::string& details);
};

} // namespace ecu_infotainment

#endif // MEDIA_PLAYER_H