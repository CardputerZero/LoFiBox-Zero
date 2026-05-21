// SPDX-License-Identifier: GPL-3.0-or-later

#include "playback/playback_controller.h"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <optional>
#include <random>
#include <utility>

#include "app/remote_media_contract.h"

namespace lofibox::app {

void PlaybackController::setServices(RuntimeServices services)
{
    services_ = std::move(services);
    enrichment_.setServices(services_);
    runtime_.setServices(&services_);
}

void PlaybackController::setPlaybackStartedRecorder(PlaybackStartedRecorder recorder)
{
    playback_started_recorder_ = std::move(recorder);
}

void PlaybackController::setDspProfile(::lofibox::audio::dsp::DspChainProfile profile)
{
    runtime_.setDspProfile(std::move(profile));
}

const PlaybackSession& PlaybackController::session() const noexcept
{
    return session_;
}

PlaybackSession& PlaybackController::mutableSession() noexcept
{
    return session_;
}

const QueueState& PlaybackController::queue() const noexcept
{
    return queue_;
}

void PlaybackController::update(double delta_seconds, LibraryController& library_controller)
{
    enrichment_.applyPending(library_controller, session_);
    runtime_.tick(session_, queue_, library_controller, delta_seconds, [this, &library_controller](int index) {
        return playQueueIndex(library_controller, index);
    });
}

void PlaybackController::update(double delta_seconds, LibraryController& library_controller, const RemoteTrackStarter& remote_starter)
{
    enrichment_.applyPending(library_controller, session_);
    runtime_.tick(session_, queue_, library_controller, delta_seconds, [this, &library_controller, &remote_starter](int index) {
        return playQueueIndex(library_controller, index, remote_starter);
    });
}

bool PlaybackController::startTrack(LibraryController& library_controller, int track_id)
{
    rebuildQueueForCurrentSongs(library_controller, track_id);
    return playQueueIndex(library_controller, queue_.active_index);
}

void PlaybackController::prepareQueueForTrack(const LibraryController& library_controller, int track_id)
{
    rebuildQueueForCurrentSongs(library_controller, track_id);
}

bool PlaybackController::startRemoteStream(const ResolvedRemoteStream& stream, const RemoteTrack& track, const std::string& source)
{
    queue_ = {};
    session_.current_track_id.reset();
    session_.current_stream_title = track.title.empty() ? std::string("UNKNOWN") : track.title;
    session_.current_stream_source = source;
    session_.current_stream_artist = track.artist;
    session_.current_stream_album = track.album;
    session_.current_stream_duration_seconds = track.duration_seconds;
    session_.current_stream_url_redacted = stream.diagnostics.resolved_url_redacted;
    session_.current_stream_artwork_url = track.artwork_url;
    session_.current_stream_live = stream.diagnostics.live;
    session_.status = PlaybackStatus::Playing;
    runtime_.beginTrack(session_);
    session_.current_artwork.reset();
    session_.lyrics_lookup_pending = false;
    if (!track.lyrics_plain.empty()) {
        session_.current_lyrics.plain = track.lyrics_plain;
    }
    if (!track.lyrics_synced.empty()) {
        session_.current_lyrics.synced = track.lyrics_synced;
    }
    session_.current_lyrics.source = track.lyrics_source.empty() ? source : track.lyrics_source;
    const bool started = runtime_.startBackendUri(stream.url, session_);
    if (!started) {
        session_.status = PlaybackStatus::Paused;
    }
    return started;
}

bool PlaybackController::startRemoteLibraryTrack(
    const ResolvedRemoteStream& stream,
    TrackRecord& track,
    const RemoteServerProfile& profile,
    const RemoteTrack& remote_track,
    const std::string& source,
    bool cache_remote_facts)
{
    session_.current_track_id = track.id;
    session_.current_stream_title.clear();
    session_.current_stream_source = source;
    session_.current_stream_artist.clear();
    session_.current_stream_album.clear();
    session_.current_stream_duration_seconds = 0;
    session_.current_stream_url_redacted = stream.diagnostics.resolved_url_redacted;
    session_.current_stream_artwork_url = remote_track.artwork_url;
    session_.current_stream_live = stream.diagnostics.live;
    session_.status = PlaybackStatus::Playing;
    runtime_.beginTrack(session_);
    session_.current_artwork.reset();
    const bool needs_remote_governance = remoteTrackNeedsLocalMetadataGovernance(remote_track);
    session_.lyrics_lookup_pending = needs_remote_governance
        && remote_track.lyrics_plain.empty()
        && remote_track.lyrics_synced.empty()
        && track.lyrics_plain.empty()
        && track.lyrics_synced.empty();
    if (!remote_track.lyrics_plain.empty()) {
        session_.current_lyrics.plain = remote_track.lyrics_plain;
    } else if (!track.lyrics_plain.empty()) {
        session_.current_lyrics.plain = track.lyrics_plain;
    }
    if (!remote_track.lyrics_synced.empty()) {
        session_.current_lyrics.synced = remote_track.lyrics_synced;
    } else if (!track.lyrics_synced.empty()) {
        session_.current_lyrics.synced = track.lyrics_synced;
    }
    session_.current_lyrics.source = !remote_track.lyrics_source.empty()
        ? remote_track.lyrics_source
        : (!track.lyrics_source.empty() ? track.lyrics_source : source);
    const bool started = runtime_.startBackendUri(stream.url, session_);
    if (!started) {
        session_.status = PlaybackStatus::Paused;
    } else {
        recordPlaybackStarted(track);
        if (needs_remote_governance) {
            enrichment_.requestRemote(profile, remote_track, track.id, stream.url, cache_remote_facts);
        }
    }
    return started;
}

bool PlaybackController::playQueueIndex(LibraryController& library_controller, int queue_index)
{
    if (queue_index < 0 || queue_index >= static_cast<int>(queue_.active_ids.size())) {
        return false;
    }

    auto* track = library_controller.findMutableTrack(queue_.active_ids[static_cast<std::size_t>(queue_index)]);
    if (!track) {
        return false;
    }
    if (track->remote) {
        return false;
    }

    queue_.active_index = queue_index;
    session_.current_track_id = track->id;
    session_.current_stream_title.clear();
    session_.current_stream_source.clear();
    session_.current_stream_artist.clear();
    session_.current_stream_album.clear();
    session_.current_stream_duration_seconds = 0;
    session_.current_stream_url_redacted.clear();
    session_.current_stream_artwork_url.clear();
    session_.current_stream_live = false;
    session_.status = PlaybackStatus::Playing;
    runtime_.beginTrack(session_);
    session_.current_artwork.reset();
    const bool started = runtime_.startBackend(track->path, session_);
    if (!started) {
        session_.status = PlaybackStatus::Paused;
        session_.audio_active = false;
        session_.visualization_pending = false;
        session_.visualization_frame = {};
        return false;
    }
    refreshMetadata(library_controller, MetadataReadMode::LocalOnly);
    refreshArtwork(library_controller, ArtworkReadMode::LocalOnly);
    recordPlaybackStarted(*track);
    enrichment_.request(*track);
    return true;
}

bool PlaybackController::playQueueIndex(LibraryController& library_controller, int queue_index, const RemoteTrackStarter& remote_starter)
{
    if (queue_index < 0 || queue_index >= static_cast<int>(queue_.active_ids.size())) {
        return false;
    }

    if (const auto* track = library_controller.findTrack(queue_.active_ids[static_cast<std::size_t>(queue_index)]);
        track && track->remote) {
        if (!remote_starter) {
            return false;
        }
        queue_.active_index = queue_index;
        return remote_starter(track->id);
    }

    return playQueueIndex(library_controller, queue_index);
}

bool PlaybackController::stepQueue(LibraryController& library_controller, int delta)
{
    if (queue_.active_ids.empty()) {
        return false;
    }
    return runtime_.stepQueue(queue_, session_, delta, [this, &library_controller](int index) {
        return playQueueIndex(library_controller, index);
    });
}

bool PlaybackController::stepQueue(LibraryController& library_controller, int delta, const RemoteTrackStarter& remote_starter)
{
    if (queue_.active_ids.empty()) {
        return false;
    }
    return runtime_.stepQueue(queue_, session_, delta, [this, &library_controller, &remote_starter](int index) {
        return playQueueIndex(library_controller, index, remote_starter);
    });
}

bool PlaybackController::jumpQueue(LibraryController& library_controller, int queue_index)
{
    return playQueueIndex(library_controller, queue_index);
}

bool PlaybackController::jumpQueue(LibraryController& library_controller, int queue_index, const RemoteTrackStarter& remote_starter)
{
    return playQueueIndex(library_controller, queue_index, remote_starter);
}

void PlaybackController::clearQueue() noexcept
{
    queue_ = {};
}

void PlaybackController::pause() noexcept
{
    if (session_.status == PlaybackStatus::Playing) {
        runtime_.pause(session_);
    }
}

void PlaybackController::resume() noexcept
{
    if (session_.status == PlaybackStatus::Paused) {
        runtime_.resume(session_);
    }
}

void PlaybackController::stop() noexcept
{
    runtime_.stop(session_);
}

bool PlaybackController::seek(LibraryController& library_controller, double seconds)
{
    if (!session_.current_track_id) {
        return false;
    }
    auto* track = library_controller.findMutableTrack(*session_.current_track_id);
    if (track == nullptr || track->remote) {
        return false;
    }
    return runtime_.seekBackend(track->path, seconds, session_);
}

void PlaybackController::togglePlayPause() noexcept
{
    if (session_.status == PlaybackStatus::Playing) {
        pause();
    } else if (session_.status == PlaybackStatus::Paused) {
        resume();
    }
}

void PlaybackController::setShuffleEnabled(bool enabled)
{
    if (session_.shuffle_enabled == enabled) {
        return;
    }
    session_.shuffle_enabled = enabled;
    rebuildActiveQueueForMode(session_.current_track_id);
}

void PlaybackController::toggleShuffle()
{
    setShuffleEnabled(!session_.shuffle_enabled);
}

void PlaybackController::setRepeatAll(bool enabled) noexcept
{
    session_.repeat_all = enabled;
    if (enabled) {
        session_.repeat_one = false;
    }
}

void PlaybackController::setRepeatOne(bool enabled) noexcept
{
    session_.repeat_one = enabled;
    if (enabled) {
        session_.repeat_all = false;
    }
}

void PlaybackController::cycleRepeatMode() noexcept
{
    if (!session_.repeat_all && !session_.repeat_one) {
        setRepeatAll(true);
    } else if (session_.repeat_all) {
        setRepeatOne(true);
    } else {
        session_.repeat_all = false;
        session_.repeat_one = false;
    }
}

void PlaybackController::rebuildQueueForCurrentSongs(const LibraryController& library_controller, int selected_track_id)
{
    queue_.base_ids = library_controller.trackIdsForCurrentSongs();
    if (queue_.base_ids.empty()) {
        queue_.base_ids = library_controller.allSongIdsSorted();
    }
    rebuildActiveQueueForMode(selected_track_id);
}

void PlaybackController::rebuildActiveQueueForMode(std::optional<int> selected_track_id)
{
    queue_.active_ids = queue_.base_ids;
    if (session_.shuffle_enabled && queue_.active_ids.size() > 1) {
        static std::mt19937 rng{std::random_device{}()};
        std::shuffle(queue_.active_ids.begin(), queue_.active_ids.end(), rng);
    }
    const auto it = selected_track_id
        ? std::find(queue_.active_ids.begin(), queue_.active_ids.end(), *selected_track_id)
        : queue_.active_ids.end();
    queue_.active_index = it == queue_.active_ids.end() ? 0 : static_cast<int>(std::distance(queue_.active_ids.begin(), it));
}

void PlaybackController::refreshArtwork(const LibraryController& library_controller, ArtworkReadMode mode)
{
    session_.current_artwork.reset();
    if (!session_.current_track_id) {
        return;
    }
    if (const auto* track = library_controller.findTrack(*session_.current_track_id)) {
        if (track->remote && !track->artwork_url.empty()) {
            const auto cache_key = "remote-media-emby-"
                + track->remote_profile_id + "-"
                + track->remote_track_id;
            session_.current_artwork = services_.metadata.artwork_provider->readRemoteIdentity(
                cache_key, track->path, mode, track->artwork_url);
        } else {
            session_.current_artwork = services_.metadata.artwork_provider->read(track->path, mode);
        }
    }
}

void PlaybackController::refreshMetadata(LibraryController& library_controller, MetadataReadMode mode)
{
    if (!session_.current_track_id) {
        return;
    }
    auto* track = library_controller.findMutableTrack(*session_.current_track_id);
    if (!track) {
        return;
    }

    const auto metadata = services_.metadata.metadata_provider->read(track->path, mode);
    PlaybackEnrichmentCoordinator::applyMetadataToTrack(*track, metadata);
}

void PlaybackController::recordPlaybackStarted(TrackRecord& track)
{
    if (playback_started_recorder_) {
        playback_started_recorder_(track);
        return;
    }
    ++track.play_count;
    track.last_played = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch())
                            .count();
}

} // namespace lofibox::app
