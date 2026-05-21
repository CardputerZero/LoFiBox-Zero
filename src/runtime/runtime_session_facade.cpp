// SPDX-License-Identifier: GPL-3.0-or-later

#include "runtime/runtime_session_facade.h"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <iostream>
#include <optional>
#include <vector>

namespace lofibox::runtime {
namespace {

RemoteSessionSnapshot remoteSessionSnapshotFor(
    const app::RemoteServerProfile& profile,
    const app::ResolvedRemoteStream& stream,
    const app::RemoteTrack& track,
    const std::string& source)
{
    RemoteSessionSnapshot snapshot{};
    snapshot.profile_id = profile.id;
    snapshot.source_label = source.empty() ? track.source_label : source;
    snapshot.connection_status = stream.diagnostics.connection_status.empty()
        ? "READY"
        : stream.diagnostics.connection_status;
    snapshot.stream_resolved = !stream.url.empty();
    snapshot.redacted_url = stream.diagnostics.resolved_url_redacted;
    snapshot.buffer_state = stream.diagnostics.connected ? "READY" : "UNCONFIRMED";
    snapshot.recovery_action = stream.diagnostics.connected ? "NONE" : "RECONNECT";
    snapshot.bitrate_kbps = stream.diagnostics.bitrate_kbps;
    snapshot.codec = stream.diagnostics.codec;
    snapshot.live = stream.diagnostics.live;
    snapshot.seekable = stream.seekable && stream.diagnostics.seekable;
    if (!stream.diagnostics.source_name.empty()) {
        snapshot.source_label = stream.diagnostics.source_name;
    }
    return snapshot;
}

app::RemoteTrack remoteTrackFromLibraryTrack(
    application::AppServiceRegistry services,
    const app::RemoteServerProfile& profile,
    const app::TrackRecord& track)
{
    app::RemoteTrack remote_track{};
    remote_track.id = track.remote_track_id;
    remote_track.title = track.title;
    remote_track.artist = track.artist;
    remote_track.album = track.album;
    remote_track.duration_seconds = track.duration_seconds;
    remote_track.source_id = track.remote_profile_id.empty() ? profile.id : track.remote_profile_id;
    remote_track.source_label = track.source_label.empty()
        ? services.sourceProfiles().profileLabel(profile)
        : track.source_label;
    remote_track.artwork_key = track.artwork_key;
    remote_track.artwork_url = track.artwork_url;
    remote_track.lyrics_plain = track.lyrics_plain;
    remote_track.lyrics_synced = track.lyrics_synced;
    remote_track.lyrics_source = track.lyrics_source;
    remote_track.fingerprint = track.fingerprint;
    return remote_track;
}

std::optional<std::size_t> findProfileIndex(
    const std::vector<app::RemoteServerProfile>& profiles,
    const std::string& profile_id)
{
    const auto profile_it = std::find_if(profiles.begin(), profiles.end(), [&profile_id](const app::RemoteServerProfile& profile) {
        return profile.id == profile_id;
    });
    if (profile_it == profiles.end()) {
        return std::nullopt;
    }
    return static_cast<std::size_t>(std::distance(profiles.begin(), profile_it));
}

int findRemoteLibraryTrackId(
    application::AppServiceRegistry services,
    const std::string& profile_id,
    const std::string& item_id)
{
    for (const auto& library_track : services.libraryQueries().model().tracks) {
        if (library_track.remote
            && library_track.remote_profile_id == profile_id
            && library_track.remote_track_id == item_id) {
            return library_track.id;
        }
    }
    return 0;
}

bool inactivePausedTrackAtKnownEnd(const app::PlaybackSession& session, const app::TrackRecord& track) noexcept
{
    if (session.status != app::PlaybackStatus::Paused || session.audio_active || track.duration_seconds <= 0) {
        return false;
    }
    const auto restart_threshold = std::max(0.0, static_cast<double>(track.duration_seconds) - 0.25);
    return session.elapsed_seconds >= restart_threshold;
}

app::RemoteTrack catalogTrackForRemoteItem(
    application::AppServiceRegistry services,
    app::RemoteServerProfile& profile,
    std::size_t profile_count,
    const std::string& item_id)
{
    const auto library = services.remoteBrowseQueries().libraryTracks(profile, profile_count, 1000);
    for (const auto& track : library.playable_tracks) {
        if (track.id == item_id) {
            return track;
        }
    }

    app::RemoteTrack remote_track{};
    remote_track.id = item_id;
    remote_track.title = item_id;
    remote_track.source_id = profile.id;
    remote_track.source_label = services.sourceProfiles().profileLabel(profile);
    return remote_track;
}

} // namespace

RuntimeSessionFacade::RuntimeSessionFacade(application::AppServiceRegistry services, EqRuntimeState& eq, SettingsRuntimeState& settings) noexcept
    : services_(services),
      playback_(services_),
      queue_(services_),
      eq_(services_, eq),
      settings_(settings)
{
}

PlaybackRuntime& RuntimeSessionFacade::playback() noexcept
{
    return playback_;
}

const PlaybackRuntime& RuntimeSessionFacade::playback() const noexcept
{
    return playback_;
}

QueueRuntime& RuntimeSessionFacade::queue() noexcept
{
    return queue_;
}

const QueueRuntime& RuntimeSessionFacade::queue() const noexcept
{
    return queue_;
}

EqRuntime& RuntimeSessionFacade::eq() noexcept
{
    return eq_;
}

const EqRuntime& RuntimeSessionFacade::eq() const noexcept
{
    return eq_;
}

RemoteSessionRuntime& RuntimeSessionFacade::remote() noexcept
{
    return remote_;
}

const RemoteSessionRuntime& RuntimeSessionFacade::remote() const noexcept
{
    return remote_;
}

SettingsRuntime& RuntimeSessionFacade::settings() noexcept
{
    return settings_;
}

const SettingsRuntime& RuntimeSessionFacade::settings() const noexcept
{
    return settings_;
}

bool RuntimeSessionFacade::playFirstAvailable()
{
    const auto& session = services_.playbackStatus().session();
    if (session.status == app::PlaybackStatus::Paused && session.current_track_id) {
        if (resumePlayback()) {
            return true;
        }
        return startTrack(*session.current_track_id);
    }
    if (session.status == app::PlaybackStatus::Playing) {
        return session.audio_active;
    }
    if (session.current_track_id) {
        return startTrack(*session.current_track_id);
    }
    return playback_.playFirstAvailable();
}

bool RuntimeSessionFacade::resumePlayback()
{
    const auto& session = services_.playbackStatus().session();
    if (session.status == app::PlaybackStatus::Paused && session.current_track_id) {
        if (const auto* track = services_.libraryQueries().findTrack(*session.current_track_id);
            track != nullptr && inactivePausedTrackAtKnownEnd(session, *track)) {
            return startTrack(*session.current_track_id);
        }
    }
    return playback_.resume();
}

bool RuntimeSessionFacade::togglePlayPause()
{
    const auto& session = services_.playbackStatus().session();
    if (session.status == app::PlaybackStatus::Playing) {
        playback_.pause();
        return services_.playbackStatus().session().status == app::PlaybackStatus::Paused;
    }
    if (session.status == app::PlaybackStatus::Paused) {
        return resumePlayback();
    }
    return false;
}

bool RuntimeSessionFacade::startTrack(int track_id)
{
    const auto* track = services_.libraryQueries().findTrack(track_id);
    if (track == nullptr) {
        return false;
    }
    if (!track->remote) {
        return playback_.startTrack(track_id);
    }
    if (track->remote_profile_id.empty() || track->remote_track_id.empty()) {
        return false;
    }

    auto profiles = services_.sourceProfiles().loadProfiles();
    const auto profile_index = findProfileIndex(profiles, track->remote_profile_id);
    if (!profile_index) {
        return false;
    }
    auto& profile = profiles[*profile_index];
    const auto session = services_.sourceProfiles().probe(profile, profiles.size()).session;
    if (!session.available) {
        return false;
    }

    auto remote_track = remoteTrackFromLibraryTrack(services_, profile, *track);
    services_.remoteBrowseQueries().rememberTrackFacts(profile, remote_track);
    const auto resolve = services_.remoteBrowseQueries().resolve(profile, session, remote_track);
    if (!resolve.stream) {
        return false;
    }
    remote_track = resolve.track;
    services_.libraryMutations().mergeRemoteTracks(profile, std::vector<app::RemoteTrack>{remote_track});

    const auto source = !remote_track.source_label.empty()
        ? remote_track.source_label
        : services_.sourceProfiles().profileLabel(profile);
    const bool started = playback_.startRemoteLibraryTrack(RemotePlayResolvedLibraryTrackPayload{
        track_id,
        profile,
        remote_track,
        *resolve.stream,
        source,
        services_.sourceProfiles().keepsLocalFacts(profile.kind),
        remoteSessionSnapshotFor(profile, *resolve.stream, remote_track, source)});
    if (started) {
        remote_.setSnapshot(remoteSessionSnapshotFor(profile, *resolve.stream, remote_track, source));
    }
    return started;
}

bool RuntimeSessionFacade::startTrackWithContext(int track_id, const PlaybackStartTrackPayload& context)
{
    std::cerr << "[webui] startTrackWithContext: track_id=" << track_id
              << " album=\"" << context.album << "\""
              << " artist=\"" << context.artist << "\"\n";

    auto& lib = services_.libraryController();

    if (!context.album.empty()) {
        const auto& model = services_.libraryQueries().model();
        std::cerr << "[webui] searching " << model.albums.size() << " albums for track " << track_id << "\n";

        bool found = false;
        for (const auto& album : model.albums) {
            auto it = std::find(album.track_ids.begin(), album.track_ids.end(), track_id);
            if (it != album.track_ids.end()) {
                std::cerr << "[webui] FOUND album \"" << album.album
                          << "\" (" << album.track_ids.size() << " tracks), setting context\n";
                lib.setSongsContextAlbum(album);
                found = true;
                break;
            }
        }
        if (!found) {
            std::cerr << "[webui] album NOT found for track " << track_id << "\n";
        }
    } else if (!context.genre.empty()) {
        const auto& model = services_.libraryQueries().model();
        const auto ids = application::LibraryQueryService::idsForGenre(model, context.genre);
        lib.setSongsContextFiltered(app::SongsMode::Genre, context.genre, ids);
        std::cerr << "[webui] set genre context \"" << context.genre << "\" with " << ids.size() << " tracks\n";
    } else {
        std::cerr << "[webui] no album or genre in payload, falling through to plain startTrack\n";
    }

    const auto track_ids_before = lib.trackIdsForCurrentSongs();
    std::cerr << "[webui] trackIdsForCurrentSongs() after context set: " << track_ids_before.size() << " ids\n";

    services_.playbackCommands().prepareQueueForTrack(track_id);

    const bool ok = startTrack(track_id);
    std::cerr << "[webui] startTrack(" << track_id << ") returned " << (ok ? "true" : "false") << "\n";
    return ok;
}

bool RuntimeSessionFacade::startRemoteItem(const std::string& profile_id, const std::string& item_id)
{
    if (profile_id.empty() || item_id.empty()) {
        return false;
    }

    auto profiles = services_.sourceProfiles().loadProfiles();
    const auto profile_index = findProfileIndex(profiles, profile_id);
    if (!profile_index) {
        return false;
    }
    auto& profile = profiles[*profile_index];
    const auto session = services_.sourceProfiles().probe(profile, profiles.size()).session;
    if (!session.available) {
        return false;
    }

    app::RemoteTrack remote_track = catalogTrackForRemoteItem(services_, profile, profiles.size(), item_id);
    if (remote_track.id.empty()) {
        remote_track.id = item_id;
    }
    if (remote_track.title.empty()) {
        remote_track.title = item_id;
    }
    if (remote_track.source_id.empty()) {
        remote_track.source_id = profile.id;
    }
    if (remote_track.source_label.empty()) {
        remote_track.source_label = services_.sourceProfiles().profileLabel(profile);
    }
    services_.remoteBrowseQueries().rememberTrackFacts(profile, remote_track);

    const auto resolve = services_.remoteBrowseQueries().resolve(profile, session, remote_track);
    if (!resolve.stream) {
        return false;
    }
    remote_track = resolve.track;
    if (remote_track.title.empty()) {
        remote_track.title = item_id;
    }
    if (remote_track.source_id.empty()) {
        remote_track.source_id = profile.id;
    }
    if (remote_track.source_label.empty()) {
        remote_track.source_label = services_.sourceProfiles().profileLabel(profile);
    }
    services_.libraryMutations().mergeRemoteTracks(profile, std::vector<app::RemoteTrack>{remote_track});

    const int local_track_id = findRemoteLibraryTrackId(services_, profile.id, remote_track.id);
    if (local_track_id == 0) {
        return false;
    }
    const auto source = !remote_track.source_label.empty()
        ? remote_track.source_label
        : services_.sourceProfiles().profileLabel(profile);
    const bool started = playback_.startRemoteLibraryTrack(RemotePlayResolvedLibraryTrackPayload{
        local_track_id,
        profile,
        remote_track,
        *resolve.stream,
        source,
        services_.sourceProfiles().keepsLocalFacts(profile.kind),
        remoteSessionSnapshotFor(profile, *resolve.stream, remote_track, source)});
    if (started) {
        remote_.setSnapshot(remoteSessionSnapshotFor(profile, *resolve.stream, remote_track, source));
    }
    return started;
}

bool RuntimeSessionFacade::stepQueue(int delta)
{
    return services_.queueCommands().step(delta, [this](int track_id) {
        return startTrack(track_id);
    });
}

bool RuntimeSessionFacade::jumpQueue(int queue_index)
{
    return services_.queueCommands().jump(queue_index, [this](int track_id) {
        return startTrack(track_id);
    });
}

bool RuntimeSessionFacade::refreshLibrary()
{
    return services_.libraryMutations().beginAsyncRefreshConfiguredLibrary();
}

void RuntimeSessionFacade::tick(double delta_seconds)
{
    playback_.update(delta_seconds);
    creator_.update(services_, delta_seconds);
}

RuntimeSnapshot RuntimeSessionFacade::snapshot(std::uint64_t version) const
{
    return snapshots_.assemble(playback_, queue_, eq_, remote_, settings_, services_, creator_, version);
}

} // namespace lofibox::runtime
