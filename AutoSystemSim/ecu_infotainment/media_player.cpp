// AutoSystemSim/ecu_infotainment/media_player.cpp
#include "media_player.h"
#include <algorithm> // For std::min/max
#include <random>    // For simulating errors

namespace ecu_infotainment {

// Helper to convert MediaSource enum to string for logging
const char* mediaSourceToString(MediaSource src) {
    switch (src) {
        case MediaSource::USB: return "USB";
        case MediaSource::BLUETOOTH: return "Bluetooth";
        case MediaSource::RADIO_FM: return "Radio FM";
        case MediaSource::RADIO_AM: return "Radio AM";
        case MediaSource::AUX: return "AUX";
        case MediaSource::NONE: return "None";
        default: return "Unknown Source";
    }
}

// Helper to convert PlaybackStatus enum to string for logging
const char* playbackStatusToString(PlaybackStatus status) {
    switch (status) {
        case PlaybackStatus::STOPPED: return "STOPPED";
        case PlaybackStatus::PLAYING: return "PLAYING";
        case PlaybackStatus::PAUSED: return "PAUSED";
        case PlaybackStatus::BUFFERING: return "BUFFERING";
        case PlaybackStatus::ERROR_SOURCE_UNAVAILABLE: return "ERROR_SOURCE_UNAVAILABLE";
        case PlaybackStatus::ERROR_TRACK_UNREADABLE: return "ERROR_TRACK_UNREADABLE";
        default: return "Unknown Status";
    }
}


MediaPlayer::MediaPlayer() :
    current_source_(MediaSource::NONE),
    current_status_(PlaybackStatus::STOPPED),
    volume_level_(50), // Default volume
    muted_(false),
    current_track_index_(-1),
    paused_elapsed_time_(0),
    current_fm_frequency_(0.0),
    current_am_frequency_(0.0)
{
    LOG_INFO("MediaPlayer: Initializing. Default volume: %d%%.", volume_level_);
}

MediaPlayer::~MediaPlayer() {
    LOG_INFO("MediaPlayer: Shutting down. Current source: %s, Status: %s.",
             mediaSourceToString(current_source_), playbackStatusToString(current_status_));
}

bool MediaPlayer::selectSource(MediaSource new_source) {
    LOG_INFO("MediaPlayer: Request to select source: %s.", mediaSourceToString(new_source));
    if (current_source_ == new_source) {
        LOG_DEBUG("MediaPlayer: Source %s is already active.", mediaSourceToString(new_source));
        return true;
    }

    stop(); // Stop playback on current source before switching

    current_source_ = new_source;
    current_track_index_ = -1; // Reset track index
    current_playlist_.clear();  // Clear playlist from previous source
    current_status_ = PlaybackStatus::STOPPED; // Default to stopped for new source

    LOG_INFO("MediaPlayer: Switched to source %s.", mediaSourceToString(current_source_));

    // Simulate source availability check
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> distrib(1, 100);

    switch (current_source_) {
        case MediaSource::USB:
            if (distrib(gen) <= 10) { // 10% chance USB not available/readable
                reportPlaybackError(PlaybackStatus::ERROR_SOURCE_UNAVAILABLE, "USB device not detected or unreadable.");
                return false;
            }
            LOG_INFO("MediaPlayer: USB source selected. Please load a playlist or select a track.");
            // Dummy playlist for USB for now
            loadPlaylist({
                {"USB Track 1", "Artist A", "Album X", std::chrono::seconds(180), 1},
                {"USB Track 2", "Artist B", "Album Y", std::chrono::seconds(220), 2},
                {"Bad USB File", "Corrupted", "Unknown", std::chrono::seconds(10), 3} // For error simulation
            });
            break;
        case MediaSource::BLUETOOTH:
            if (distrib(gen) <= 5) { // 5% chance Bluetooth connection failed
                reportPlaybackError(PlaybackStatus::ERROR_SOURCE_UNAVAILABLE, "Bluetooth connection failed or device not paired.");
                return false;
            }
            LOG_INFO("MediaPlayer: Bluetooth source selected. Awaiting stream or playlist.");
             loadPlaylist({
                {"BT Song Alpha", "BT Artist", "BT Album", std::chrono::seconds(200), 1},
                {"BT Song Beta", "BT Artist", "BT Album", std::chrono::seconds(240), 2}
            });
            break;
        case MediaSource::RADIO_FM:
            LOG_INFO("MediaPlayer: FM Radio source selected. Please tune to a station.");
            tuneRadio(98.5, MediaSource::RADIO_FM); // Default FM station
            break;
        case MediaSource::RADIO_AM:
            LOG_INFO("MediaPlayer: AM Radio source selected. Please tune to a station.");
            tuneRadio(740, MediaSource::RADIO_AM); // Default AM station
            break;
        case MediaSource::AUX:
            LOG_INFO("MediaPlayer: AUX source selected. Playback controlled by external device.");
            current_status_ = PlaybackStatus::PLAYING; // Assume AUX is always "playing" from our perspective
            break;
        case MediaSource::NONE:
            LOG_INFO("MediaPlayer: No media source selected.");
            break;
        default:
            LOG_WARNING("MediaPlayer: Unknown media source selected: %d", static_cast<int>(current_source_));
            break;
    }
    return true;
}

MediaSource MediaPlayer::getCurrentSource() const {
    LOG_DEBUG("MediaPlayer: getCurrentSource() -> %s.", mediaSourceToString(current_source_));
    return current_source_;
}

bool MediaPlayer::playTrackAtIndex(int index) {
    if (index < 0 || index >= static_cast<int>(current_playlist_.size())) {
        reportPlaybackError(PlaybackStatus::ERROR_TRACK_UNREADABLE, "Invalid track index: " + std::to_string(index));
        return false;
    }

    // Simulate track readability issue
    const TrackInfo& track = current_playlist_[index];
    if (track.title == "Bad USB File") { // Specific track to simulate error
         reportPlaybackError(PlaybackStatus::ERROR_TRACK_UNREADABLE, "Cannot read track: " + track.title + " (simulated corruption).");
         current_track_index_ = index; // So user knows which track failed
         return false;
    }


    current_track_index_ = index;
    current_status_ = PlaybackStatus::PLAYING;
    track_start_time_ = std::chrono::steady_clock::now();
    paused_elapsed_time_ = std::chrono::seconds(0);

    LOG_INFO("MediaPlayer: Playing track #%d: '%s' by '%s'. Duration: %llds.",
             track.track_number, track.title.c_str(), track.artist.c_str(), track.duration.count());
    return true;
}

bool MediaPlayer::play() {
    LOG_INFO("MediaPlayer: Play command received.");
    if (muted_) {
        LOG_DEBUG("MediaPlayer: Player is muted. Sound will not be audible until unmuted.");
    }
    switch (current_source_) {
        case MediaSource::USB:
        case MediaSource::BLUETOOTH:
            if (current_status_ == PlaybackStatus::PLAYING) {
                LOG_DEBUG("MediaPlayer: Already playing.");
                return true;
            }
            if (current_playlist_.empty()) {
                reportPlaybackError(PlaybackStatus::ERROR_SOURCE_UNAVAILABLE, "No playlist loaded for " + std::string(mediaSourceToString(current_source_)));
                return false;
            }
            if (current_status_ == PlaybackStatus::PAUSED && current_track_index_ != -1) {
                // Resume from pause
                current_status_ = PlaybackStatus::PLAYING;
                // Adjust start time to account for paused duration
                track_start_time_ = std::chrono::steady_clock::now() - paused_elapsed_time_;
                LOG_INFO("MediaPlayer: Resuming track '%s'.", current_playlist_[current_track_index_].title.c_str());
            } else {
                // Start playing first track or current track if one was selected/stopped
                int track_to_play = (current_track_index_ == -1 || current_track_index_ >= static_cast<int>(current_playlist_.size())) ? 0 : current_track_index_;
                return playTrackAtIndex(track_to_play);
            }
            break;
        case MediaSource::RADIO_FM:
        case MediaSource::RADIO_AM:
            current_status_ = PlaybackStatus::PLAYING; // Radio is conceptually always "playing" a station
            LOG_INFO("MediaPlayer: Playing radio station %.1f %s.",
                     (current_source_ == MediaSource::RADIO_FM ? current_fm_frequency_ : current_am_frequency_),
                     (current_source_ == MediaSource::RADIO_FM ? "MHz" : "kHz"));
            break;
        case MediaSource::AUX:
            current_status_ = PlaybackStatus::PLAYING; // AUX is controlled externally
            LOG_INFO("MediaPlayer: AUX source is active.");
            break;
        case MediaSource::NONE:
            reportPlaybackError(PlaybackStatus::ERROR_SOURCE_UNAVAILABLE, "No media source selected to play.");
            return false;
        default:
            LOG_WARNING("MediaPlayer: Play command on unhandled source: %s.", mediaSourceToString(current_source_));
            return false;
    }
    return true;
}

bool MediaPlayer::pause() {
    LOG_INFO("MediaPlayer: Pause command received.");
    if (current_status_ == PlaybackStatus::PLAYING &&
        (current_source_ == MediaSource::USB || current_source_ == MediaSource::BLUETOOTH) &&
        current_track_index_ != -1) {
        current_status_ = PlaybackStatus::PAUSED;
        paused_elapsed_time_ = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::steady_clock::now() - track_start_time_
        );
        LOG_INFO("MediaPlayer: Paused track '%s' at %llds.",
                 current_playlist_[current_track_index_].title.c_str(),
                 paused_elapsed_time_.count());
    } else if (current_status_ == PlaybackStatus::PAUSED) {
        LOG_DEBUG("MediaPlayer: Already paused.");
    } else {
        LOG_WARNING("MediaPlayer: Cannot pause. Not playing a pausable track or source %s is not pausable.", mediaSourceToString(current_source_));
        return false;
    }
    return true;
}

bool MediaPlayer::stop() {
    LOG_INFO("MediaPlayer: Stop command received.");
    if (current_status_ != PlaybackStatus::STOPPED) {
        current_status_ = PlaybackStatus::STOPPED;
        // For track-based media, current_track_index_ remains, so play resumes this track.
        // If we wanted to reset to start of playlist, set current_track_index_ = 0 (or -1 to pick first).
        if (current_source_ == MediaSource::USB || current_source_ == MediaSource::BLUETOOTH) {
             if (current_track_index_ != -1) {
                LOG_INFO("MediaPlayer: Stopped playback of track '%s'.", current_playlist_[current_track_index_].title.c_str());
             } else {
                LOG_INFO("MediaPlayer: Playback stopped on source %s.", mediaSourceToString(current_source_));
             }
             paused_elapsed_time_ = std::chrono::seconds(0); // Reset pause time
        } else {
            LOG_INFO("MediaPlayer: Playback stopped on source %s.", mediaSourceToString(current_source_));
        }
    } else {
        LOG_DEBUG("MediaPlayer: Already stopped.");
    }
    return true;
}

bool MediaPlayer::nextTrack() {
    LOG_INFO("MediaPlayer: Next track command received.");
    if (current_source_ != MediaSource::USB && current_source_ != MediaSource::BLUETOOTH) {
        LOG_WARNING("MediaPlayer: Next track command ignored. Source %s does not support tracks.", mediaSourceToString(current_source_));
        return false;
    }
    if (current_playlist_.empty()) {
        reportPlaybackError(PlaybackStatus::ERROR_SOURCE_UNAVAILABLE, "No playlist loaded for next track.");
        return false;
    }
    int next_idx = (current_track_index_ + 1) % current_playlist_.size();
    return playTrackAtIndex(next_idx);
}

bool MediaPlayer::previousTrack() {
    LOG_INFO("MediaPlayer: Previous track command received.");
     if (current_source_ != MediaSource::USB && current_source_ != MediaSource::BLUETOOTH) {
        LOG_WARNING("MediaPlayer: Previous track command ignored. Source %s does not support tracks.", mediaSourceToString(current_source_));
        return false;
    }
    if (current_playlist_.empty()) {
        reportPlaybackError(PlaybackStatus::ERROR_SOURCE_UNAVAILABLE, "No playlist loaded for previous track.");
        return false;
    }
    // If current track played for > N seconds, restart current track, else go to prev.
    if (getCurrentTrackElapsedTime() > std::chrono::seconds(5) && current_status_ == PlaybackStatus::PLAYING) {
        LOG_DEBUG("MediaPlayer: Restarting current track '%s'.", current_playlist_[current_track_index_].title.c_str());
        return playTrackAtIndex(current_track_index_);
    }

    int prev_idx = current_track_index_ - 1;
    if (prev_idx < 0) {
        prev_idx = current_playlist_.size() - 1; // Wrap around to last track
    }
    return playTrackAtIndex(prev_idx);
}

bool MediaPlayer::seek(std::chrono::seconds position) {
    LOG_INFO("MediaPlayer: Seek command received. Target position: %llds.", position.count());
    if (current_status_ != PlaybackStatus::PLAYING && current_status_ != PlaybackStatus::PAUSED) {
        LOG_WARNING("MediaPlayer: Cannot seek. Not currently playing or paused on a track.");
        return false;
    }
    if (current_source_ != MediaSource::USB && current_source_ != MediaSource::BLUETOOTH) {
        LOG_WARNING("MediaPlayer: Seek command ignored. Source %s does not support seeking.", mediaSourceToString(current_source_));
        return false;
    }
    if (current_track_index_ == -1 || current_playlist_.empty()) {
        reportPlaybackError(PlaybackStatus::ERROR_TRACK_UNREADABLE, "No track loaded to seek in.");
        return false;
    }

    const auto& track = current_playlist_[current_track_index_];
    if (position < std::chrono::seconds(0) || position > track.duration) {
        LOG_WARNING("MediaPlayer: Invalid seek position %llds for track '%s' (duration %llds). Clamping.",
                    position.count(), track.title.c_str(), track.duration.count());
        position = std::max(std::chrono::seconds(0), std::min(position, track.duration));
    }

    track_start_time_ = std::chrono::steady_clock::now() - position;
    if (current_status_ == PlaybackStatus::PAUSED) { // If paused, update paused_elapsed_time_ to new seek position
        paused_elapsed_time_ = position;
    }
    LOG_INFO("MediaPlayer: Seeked track '%s' to %llds.", track.title.c_str(), position.count());
    return true;
}


bool MediaPlayer::setVolume(int level_percent) {
    level_percent = std::max(0, std::min(100, level_percent)); // Clamp to 0-100
    LOG_INFO("MediaPlayer: Set volume command. Level: %d%%.", level_percent);
    if (volume_level_ == level_percent && !muted_) { // If setting same volume and not unmuting
        LOG_DEBUG("MediaPlayer: Volume already at %d%%.", level_percent);
        return true;
    }
    volume_level_ = level_percent;
    if (volume_level_ > 0 && muted_) {
        LOG_INFO("MediaPlayer: Volume set to %d%%. Unmuting device.", volume_level_);
        muted_ = false;
    } else if (volume_level_ == 0 && !muted_) {
        LOG_INFO("MediaPlayer: Volume set to 0%%. Muting device.");
        muted_ = true;
    }
    LOG_INFO("MediaPlayer: Volume is now %d%%. Muted: %s.", volume_level_, muted_ ? "YES" : "NO");
    return true;
}

int MediaPlayer::getVolume() const {
    LOG_DEBUG("MediaPlayer: getVolume() -> %d%%. Muted: %s.", volume_level_, muted_ ? "YES" : "NO");
    return muted_ ? 0 : volume_level_; // Return 0 if muted, actual level otherwise
}

bool MediaPlayer::mute(bool enable_mute) {
    LOG_INFO("MediaPlayer: Mute command. Enable: %s.", enable_mute ? "YES" : "NO");
    if (muted_ == enable_mute) {
        LOG_DEBUG("MediaPlayer: Mute state already %s.", muted_ ? "ON" : "OFF");
        return true;
    }
    muted_ = enable_mute;
    LOG_INFO("MediaPlayer: Mute state is now %s. Volume level (if unmuted): %d%%.",
             muted_ ? "ON" : "OFF", volume_level_);
    return true;
}

bool MediaPlayer::isMuted() const {
    LOG_DEBUG("MediaPlayer: isMuted() -> %s.", muted_ ? "YES" : "NO");
    return muted_;
}

PlaybackStatus MediaPlayer::getPlaybackStatus() const {
    LOG_DEBUG("MediaPlayer: getPlaybackStatus() -> %s.", playbackStatusToString(current_status_));
    return current_status_;
}

TrackInfo MediaPlayer::getCurrentTrackInfo() const {
    if (current_track_index_ != -1 && !current_playlist_.empty() &&
        (current_source_ == MediaSource::USB || current_source_ == MediaSource::BLUETOOTH)) {
        LOG_DEBUG("MediaPlayer: getCurrentTrackInfo() for '%s'.", current_playlist_[current_track_index_].title.c_str());
        return current_playlist_[current_track_index_];
    }
    // For radio or other sources, TrackInfo might be different (e.g., station name, RDS data)
    // Simplified: return empty TrackInfo if not applicable
    LOG_DEBUG("MediaPlayer: getCurrentTrackInfo() -> No track info available for current source/status.");
    return {"N/A", "N/A", "N/A", std::chrono::seconds(0), 0};
}

std::chrono::seconds MediaPlayer::getCurrentTrackElapsedTime() const {
    if (current_status_ == PlaybackStatus::PLAYING && current_track_index_ != -1) {
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::steady_clock::now() - track_start_time_
        );
        LOG_VERBOSE("MediaPlayer: getCurrentTrackElapsedTime() -> %llds (PLAYING).", elapsed.count());
        return elapsed;
    } else if (current_status_ == PlaybackStatus::PAUSED && current_track_index_ != -1) {
        LOG_VERBOSE("MediaPlayer: getCurrentTrackElapsedTime() -> %llds (PAUSED).", paused_elapsed_time_.count());
        return paused_elapsed_time_;
    }
    LOG_VERBOSE("MediaPlayer: getCurrentTrackElapsedTime() -> 0s (not playing/paused or no track).");
    return std::chrono::seconds(0);
}

bool MediaPlayer::loadPlaylist(const std::vector<TrackInfo>& playlist) {
    if (current_source_ != MediaSource::USB && current_source_ != MediaSource::BLUETOOTH) {
        LOG_WARNING("MediaPlayer: Cannot load playlist. Current source %s does not support playlists.",
                    mediaSourceToString(current_source_));
        return false;
    }
    current_playlist_ = playlist;
    current_track_index_ = -1; // Reset, play will start from first track
    current_status_ = PlaybackStatus::STOPPED; // Ready to play
    LOG_INFO("MediaPlayer: Playlist with %zu tracks loaded for source %s.",
             playlist.size(), mediaSourceToString(current_source_));
    for(size_t i = 0; i < playlist.size(); ++i) {
        LOG_DEBUG("MediaPlayer: Playlist item %zu: '%s' by '%s'", i+1, playlist[i].title.c_str(), playlist[i].artist.c_str());
    }
    return true;
}

bool MediaPlayer::tuneRadio(double frequency, MediaSource radio_band) {
    if (radio_band != MediaSource::RADIO_FM && radio_band != MediaSource::RADIO_AM) {
        LOG_ERROR("MediaPlayer: Invalid radio band specified for tuning: %s", mediaSourceToString(radio_band));
        return false;
    }
    if (current_source_ != radio_band) {
        LOG_WARNING("MediaPlayer: Cannot tune %s. Current source is %s. Please select %s source first.",
                    mediaSourceToString(radio_band), mediaSourceToString(current_source_), mediaSourceToString(radio_band));
        // Or we could auto-select source:
        // selectSource(radio_band);
        return false;
    }

    if (radio_band == MediaSource::RADIO_FM) {
        current_fm_frequency_ = frequency;
        LOG_INFO("MediaPlayer: Tuned FM Radio to %.1f MHz.", current_fm_frequency_);
    } else { // RADIO_AM
        current_am_frequency_ = frequency;
        LOG_INFO("MediaPlayer: Tuned AM Radio to %.0f kHz.", current_am_frequency_);
    }
    current_status_ = PlaybackStatus::PLAYING; // Tuning to a station implies playing
    LOG_DEBUG("MediaPlayer: Radio tuned. Status set to PLAYING.");
    return true;
}

void MediaPlayer::handleTrackEnd() {
    LOG_INFO("MediaPlayer: Track '%s' ended.", current_playlist_[current_track_index_].title.c_str());
    // Logic for what to do next: repeat, shuffle, next, stop
    // Simple: go to next track, or stop if it was the last track of a non-repeating playlist
    bool repeat_playlist = false; // Could be a setting

    if (current_track_index_ + 1 < static_cast<int>(current_playlist_.size())) {
        LOG_INFO("MediaPlayer: Playing next track in playlist.");
        nextTrack(); // Internal call
    } else if (repeat_playlist) {
        LOG_INFO("MediaPlayer: End of playlist. Repeating from beginning.");
        playTrackAtIndex(0); // Internal call
    } else {
        LOG_INFO("MediaPlayer: End of playlist. Stopping playback.");
        stop(); // Internal call
    }
}

void MediaPlayer::simulateTimePassing() {
    if (current_status_ == PlaybackStatus::PLAYING &&
        (current_source_ == MediaSource::USB || current_source_ == MediaSource::BLUETOOTH) &&
        current_track_index_ != -1) {

        auto elapsed_time = getCurrentTrackElapsedTime(); // Uses steady_clock
        const auto& current_track = current_playlist_[current_track_index_];

        // Log progress every N seconds (e.g., every 30s of playback)
        static std::chrono::seconds last_logged_elapsed_time = std::chrono::seconds(-1000); // Initialize to force first log
        if (elapsed_time.count() > 0 && (elapsed_time - last_logged_elapsed_time >= std::chrono::seconds(30))) {
            LOG_DEBUG("MediaPlayer: Track '%s' progress: %llds / %llds.",
                      current_track.title.c_str(), elapsed_time.count(), current_track.duration.count());
            last_logged_elapsed_time = elapsed_time;
        }


        if (elapsed_time >= current_track.duration) {
            handleTrackEnd(); // Internal call
        }
    } else if (current_status_ == PlaybackStatus::PLAYING &&
               (current_source_ == MediaSource::RADIO_FM || current_source_ == MediaSource::RADIO_AM)) {
        // Radio just plays indefinitely, no track end to handle
        LOG_VERBOSE("MediaPlayer: Radio playback ongoing (%s).", mediaSourceToString(current_source_));
    } else if (current_status_ == PlaybackStatus::PLAYING && current_source_ == MediaSource::AUX) {
        LOG_VERBOSE("MediaPlayer: AUX playback ongoing.");
    }
}


void MediaPlayer::reportPlaybackError(PlaybackStatus error_status, const std::string& details) {
    current_status_ = error_status;
    LOG_ERROR("MediaPlayer: Playback Error (%s): %s",
              playbackStatusToString(error_status), details.c_str());
    // Potentially notify a main UI or error handling module
}

void MediaPlayer::updatePlaybackState() {
    LOG_VERBOSE("MediaPlayer: Updating playback state...");
    // This method would be called periodically by the infotainment system's main loop.
    simulateTimePassing(); // Internal call

    // Other periodic checks could go here, e.g.,
    // - checking Bluetooth connection stability
    // - checking USB device presence
    // - updating RDS info for radio
    // - handling streaming buffer for hypothetical streaming source

    // Example: Simulate random USB disconnection
    if (current_source_ == MediaSource::USB && current_status_ != PlaybackStatus::ERROR_SOURCE_UNAVAILABLE) {
        std::random_device rd;
        std::mt19937 gen(rd());
        // Low chance, e.g. 1 in 1000 update cycles
        if (std::uniform_int_distribution<>(1, 1000)(gen) == 1) {
            reportPlaybackError(PlaybackStatus::ERROR_SOURCE_UNAVAILABLE, "USB device disconnected unexpectedly (simulated).");
            stop();
        }
    }
    LOG_VERBOSE("MediaPlayer: Playback state update cycle complete. Status: %s", playbackStatusToString(current_status_));
}

} // namespace ecu_infotainment