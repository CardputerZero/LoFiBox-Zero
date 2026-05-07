// SPDX-License-Identifier: GPL-3.0-or-later

#include "app/library_controller.h"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <exception>
#include <utility>

#include "app/library_navigation_service.h"
#include "app/remote_profile_store.h"
#include "application/library_open_action_service.h"
#include "application/library_query_service.h"

namespace lofibox::app {
namespace {

std::string sortKey(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

std::string sourceLabelForProfile(const RemoteServerProfile& profile)
{
    if (!profile.name.empty()) {
        return profile.name;
    }
    auto label = remoteServerKindToString(profile.kind);
    std::transform(label.begin(), label.end(), label.begin(), [](unsigned char ch) {
        return static_cast<char>(std::toupper(ch));
    });
    return label.empty() ? std::string{"REMOTE"} : label;
}

} // namespace

LibraryController::~LibraryController()
{
    joinAsyncScan();
}

LibraryIndexState LibraryController::state() const noexcept
{
    return repository_.state();
}

const LibraryModel& LibraryController::model() const noexcept
{
    return repository_.model();
}

LibraryModel& LibraryController::mutableModel() noexcept
{
    return repository_.mutableModel();
}

void LibraryController::startLoading() noexcept
{
    repository_.markLoading();
}

void LibraryController::resetScanProgress(LibraryScanPhase phase, std::string message)
{
    scan_phase_.store(static_cast<int>(phase));
    scan_roots_total_.store(0);
    scan_roots_scanned_.store(0);
    scan_files_discovered_.store(0);
    scan_files_total_.store(0);
    scan_files_processed_.store(0);
    scan_tracks_indexed_.store(0);
    scan_degraded_.store(false);
    std::lock_guard lock{scan_progress_mutex_};
    scan_current_path_.clear();
    scan_message_ = std::move(message);
}

void LibraryController::publishScanProgress(const LibraryScanProgress& progress)
{
    scan_phase_.store(static_cast<int>(progress.phase));
    scan_roots_total_.store(progress.roots_total);
    scan_roots_scanned_.store(progress.roots_scanned);
    scan_files_discovered_.store(progress.files_discovered);
    scan_files_total_.store(progress.files_total);
    scan_files_processed_.store(progress.files_processed);
    scan_tracks_indexed_.store(progress.tracks_indexed);
    scan_degraded_.store(progress.degraded);
    std::lock_guard lock{scan_progress_mutex_};
    scan_current_path_ = progress.current_path;
    scan_message_ = progress.message;
}

LibraryScanProgress LibraryController::scanProgress() const
{
    LibraryScanProgress progress{};
    progress.phase = static_cast<LibraryScanPhase>(scan_phase_.load());
    progress.roots_total = scan_roots_total_.load();
    progress.roots_scanned = scan_roots_scanned_.load();
    progress.files_discovered = scan_files_discovered_.load();
    progress.files_total = scan_files_total_.load();
    progress.files_processed = scan_files_processed_.load();
    progress.tracks_indexed = scan_tracks_indexed_.load();
    progress.degraded = scan_degraded_.load();
    std::lock_guard lock{scan_progress_mutex_};
    progress.current_path = scan_current_path_;
    progress.message = scan_message_;
    return progress;
}

void LibraryController::refreshLibrary(const std::vector<std::filesystem::path>& media_roots, const MetadataProvider& metadata_provider)
{
    repository_.rescan(media_roots, metadata_provider);
    setSongsContextAll();
}

void LibraryController::beginAsyncRefreshLibrary(
    const std::vector<std::filesystem::path>& media_roots,
    std::shared_ptr<MetadataProvider> metadata_provider)
{
    if (scan_done_.load()) {
        (void)pollAsyncRefreshLibrary();
    }
    if (scan_running_.load()) {
        return;
    }
    joinAsyncScan();

    repository_.markLoading();
    resetScanProgress(LibraryScanPhase::DiscoveringFiles, "starting library scan");

    if (!metadata_provider) {
        LibraryScanProgress progress{};
        progress.phase = LibraryScanPhase::Failed;
        progress.message = "metadata provider unavailable";
        publishScanProgress(progress);
        repository_.markDegraded();
        return;
    }

    {
        std::lock_guard lock{async_mutex_};
        pending_scan_model_.reset();
        pending_scan_error_.clear();
    }
    scan_done_.store(false);
    scan_running_.store(true);

    auto roots = media_roots;
    try {
        scan_thread_ = std::thread([this, roots = std::move(roots), provider = std::move(metadata_provider)]() {
            try {
                auto model = repository_.rebuildModel(
                    roots,
                    *provider,
                    [this](const LibraryScanProgress& progress) {
                        publishScanProgress(progress);
                    });
                {
                    std::lock_guard lock{async_mutex_};
                    pending_scan_model_ = std::move(model);
                    pending_scan_error_.clear();
                }
            } catch (const std::exception& error) {
                LibraryScanProgress progress = scanProgress();
                progress.phase = LibraryScanPhase::Failed;
                progress.message = error.what();
                publishScanProgress(progress);
                {
                    std::lock_guard lock{async_mutex_};
                    pending_scan_model_.reset();
                    pending_scan_error_ = error.what();
                }
            } catch (...) {
                LibraryScanProgress progress = scanProgress();
                progress.phase = LibraryScanPhase::Failed;
                progress.message = "unknown library scan failure";
                publishScanProgress(progress);
                {
                    std::lock_guard lock{async_mutex_};
                    pending_scan_model_.reset();
                    pending_scan_error_ = "unknown library scan failure";
                }
            }
            scan_running_.store(false);
            scan_done_.store(true);
        });
    } catch (const std::exception& error) {
        scan_running_.store(false);
        LibraryScanProgress progress = scanProgress();
        progress.phase = LibraryScanPhase::Failed;
        progress.message = error.what();
        progress.degraded = true;
        publishScanProgress(progress);
        repository_.markDegraded();
    }
}

bool LibraryController::pollAsyncRefreshLibrary()
{
    if (!scan_done_.load()) {
        return false;
    }

    joinAsyncScan();

    std::optional<LibraryModel> model{};
    std::string error{};
    {
        std::lock_guard lock{async_mutex_};
        model = std::move(pending_scan_model_);
        pending_scan_model_.reset();
        error = std::move(pending_scan_error_);
        pending_scan_error_.clear();
    }

    if (model) {
        repository_.applyRescanModel(std::move(*model));
        setSongsContextAll();
        LibraryScanProgress progress = scanProgress();
        progress.phase = LibraryScanPhase::Complete;
        progress.message = repository_.state() == LibraryIndexState::Degraded ? "library scan completed with warnings" : "library ready";
        progress.degraded = repository_.state() == LibraryIndexState::Degraded;
        publishScanProgress(progress);
    } else {
        repository_.markDegraded();
        LibraryScanProgress progress = scanProgress();
        progress.phase = LibraryScanPhase::Failed;
        progress.message = error.empty() ? "library scan failed" : std::move(error);
        progress.degraded = true;
        publishScanProgress(progress);
    }

    scan_done_.store(false);
    scan_running_.store(false);
    return true;
}

bool LibraryController::asyncRefreshRunning() const noexcept
{
    return scan_running_.load() || scan_done_.load();
}

void LibraryController::joinAsyncScan() noexcept
{
    if (scan_thread_.joinable()) {
        scan_thread_.join();
    }
}

void LibraryController::mergeRemoteTracks(const RemoteServerProfile& profile, const std::vector<RemoteTrack>& tracks)
{
    if (tracks.empty()) {
        return;
    }

    auto& model = repository_.mutableModel();
    int next_id = 1;
    for (const auto& track : model.tracks) {
        next_id = std::max(next_id, track.id + 1);
    }

    for (const auto& remote_track : tracks) {
        if (remote_track.id.empty()) {
            continue;
        }

        auto existing = std::find_if(model.tracks.begin(), model.tracks.end(), [&](const TrackRecord& track) {
            return track.remote && track.remote_profile_id == profile.id && track.remote_track_id == remote_track.id;
        });
        if (existing != model.tracks.end()) {
            existing->title = remote_track.title.empty() ? existing->title : remote_track.title;
            existing->artist = remote_track.artist.empty() ? existing->artist : remote_track.artist;
            existing->album = remote_track.album.empty() ? existing->album : remote_track.album;
            existing->duration_seconds = remote_track.duration_seconds > 0 ? remote_track.duration_seconds : existing->duration_seconds;
            existing->source_label = remote_track.source_label.empty() ? existing->source_label : remote_track.source_label;
            existing->artwork_key = remote_track.artwork_key.empty() ? existing->artwork_key : remote_track.artwork_key;
            existing->artwork_url = remote_track.artwork_url.empty() ? existing->artwork_url : remote_track.artwork_url;
            existing->lyrics_plain = remote_track.lyrics_plain.empty() ? existing->lyrics_plain : remote_track.lyrics_plain;
            existing->lyrics_synced = remote_track.lyrics_synced.empty() ? existing->lyrics_synced : remote_track.lyrics_synced;
            existing->lyrics_source = remote_track.lyrics_source.empty() ? existing->lyrics_source : remote_track.lyrics_source;
            existing->fingerprint = remote_track.fingerprint.empty() ? existing->fingerprint : remote_track.fingerprint;
            continue;
        }

        TrackRecord track{};
        track.id = next_id++;
        track.title = remote_track.title.empty() ? std::string(kUnknownMetadata) : remote_track.title;
        track.artist = remote_track.artist.empty() ? std::string(kUnknownMetadata) : remote_track.artist;
        track.album = remote_track.album.empty() ? std::string(kUnknownMetadata) : remote_track.album;
        track.genre = std::string(kUnknownMetadata);
        track.composer = std::string(kUnknownMetadata);
        track.duration_seconds = remote_track.duration_seconds > 0 ? remote_track.duration_seconds : 180;
        track.remote = true;
        track.remote_profile_id = profile.id;
        track.remote_track_id = remote_track.id;
        track.source_label = remote_track.source_label.empty()
            ? sourceLabelForProfile(profile)
            : remote_track.source_label;
        track.artwork_key = remote_track.artwork_key;
        track.artwork_url = remote_track.artwork_url;
        track.lyrics_plain = remote_track.lyrics_plain;
        track.lyrics_synced = remote_track.lyrics_synced;
        track.lyrics_source = remote_track.lyrics_source;
        track.fingerprint = remote_track.fingerprint;
        model.tracks.push_back(std::move(track));
    }

    repository_.rebuildDerivedIndexes();
    setSongsContextAll();
}

bool LibraryController::applyRemoteTrackFacts(const RemoteServerProfile& profile, const RemoteTrack& remote_track)
{
    if (remote_track.id.empty()) {
        return false;
    }

    auto& model = repository_.mutableModel();
    auto existing = std::find_if(model.tracks.begin(), model.tracks.end(), [&](const TrackRecord& track) {
        return track.remote && track.remote_profile_id == profile.id && track.remote_track_id == remote_track.id;
    });
    if (existing == model.tracks.end()) {
        return false;
    }

    if (!remote_track.title.empty()) existing->title = remote_track.title;
    if (!remote_track.artist.empty()) existing->artist = remote_track.artist;
    if (!remote_track.album.empty()) existing->album = remote_track.album;
    if (remote_track.duration_seconds > 0) existing->duration_seconds = remote_track.duration_seconds;
    if (!remote_track.source_label.empty()) existing->source_label = remote_track.source_label;
    if (!remote_track.artwork_key.empty()) existing->artwork_key = remote_track.artwork_key;
    if (!remote_track.artwork_url.empty()) existing->artwork_url = remote_track.artwork_url;
    if (!remote_track.lyrics_plain.empty()) existing->lyrics_plain = remote_track.lyrics_plain;
    if (!remote_track.lyrics_synced.empty()) existing->lyrics_synced = remote_track.lyrics_synced;
    if (!remote_track.lyrics_source.empty()) existing->lyrics_source = remote_track.lyrics_source;
    if (!remote_track.fingerprint.empty()) existing->fingerprint = remote_track.fingerprint;
    repository_.rebuildDerivedIndexes();
    return true;
}

const TrackRecord* LibraryController::findTrack(int id) const noexcept
{
    return repository_.findTrack(id);
}

TrackRecord* LibraryController::findMutableTrack(int id) noexcept
{
    return repository_.findMutableTrack(id);
}

std::vector<int> LibraryController::allSongIdsSorted() const
{
    return ::lofibox::application::LibraryQueryService::sortedTrackIds(repository_.model(), list_context_.song_sort_mode);
}

std::vector<int> LibraryController::trackIdsForCurrentSongs() const
{
    return list_context_.songs.track_ids;
}

std::vector<int> LibraryController::playlistTrackIds() const
{
    std::vector<int> ids{};
    switch (list_context_.playlist.kind) {
    case PlaylistKind::OnTheGo:
        ids = list_context_.on_the_go_ids;
        break;
    case PlaylistKind::RecentlyAdded:
        ids = allSongIdsSorted();
        std::sort(ids.begin(), ids.end(), [this](int lhs, int rhs) {
            const auto* left = findTrack(lhs);
            const auto* right = findTrack(rhs);
            if (!left || !right) {
                return lhs < rhs;
            }
            return left->added_time > right->added_time;
        });
        break;
    case PlaylistKind::MostPlayed:
        ids = allSongIdsSorted();
        std::sort(ids.begin(), ids.end(), [this](int lhs, int rhs) {
            const auto* left = findTrack(lhs);
            const auto* right = findTrack(rhs);
            if (!left || !right) {
                return lhs < rhs;
            }
            if (left->play_count != right->play_count) {
                return left->play_count > right->play_count;
            }
            return left->title < right->title;
        });
        break;
    case PlaylistKind::RecentlyPlayed:
        ids = allSongIdsSorted();
        std::sort(ids.begin(), ids.end(), [this](int lhs, int rhs) {
            const auto* left = findTrack(lhs);
            const auto* right = findTrack(rhs);
            if (!left || !right) {
                return lhs < rhs;
            }
            if (left->last_played != right->last_played) {
                return left->last_played > right->last_played;
            }
            return left->title < right->title;
        });
        ids.erase(std::remove_if(ids.begin(), ids.end(), [this](int id) {
            const auto* track = findTrack(id);
            return !track || track->last_played == 0;
        }), ids.end());
        break;
    }
    return ids;
}

SongSortMode LibraryController::songSortMode() const noexcept
{
    return list_context_.song_sort_mode;
}

std::string LibraryController::songSortLabel() const
{
    switch (list_context_.song_sort_mode) {
    case SongSortMode::TitleAscending: return "TITLE A-Z";
    case SongSortMode::TitleDescending: return "TITLE Z-A";
    case SongSortMode::ArtistAscending: return "ARTIST A-Z";
    case SongSortMode::ArtistDescending: return "ARTIST Z-A";
    case SongSortMode::AddedAscending: return "ADDED OLD";
    case SongSortMode::AddedDescending: return "ADDED NEW";
    }
    return "TITLE A-Z";
}

void LibraryController::cycleSongSortMode()
{
    const int next = (static_cast<int>(list_context_.song_sort_mode) + 1) % 6;
    list_context_.song_sort_mode = static_cast<SongSortMode>(next);
    switch (list_context_.songs.mode) {
    case SongsMode::All:
        list_context_.songs.track_ids = allSongIdsSorted();
        break;
    case SongsMode::Album:
    case SongsMode::Genre:
    case SongsMode::Composer:
    case SongsMode::Compilation:
        std::sort(list_context_.songs.track_ids.begin(), list_context_.songs.track_ids.end(), [this](int lhs, int rhs) {
            const auto* left = findTrack(lhs);
            const auto* right = findTrack(rhs);
            if (!left || !right) {
                return lhs < rhs;
            }
            switch (list_context_.song_sort_mode) {
            case SongSortMode::TitleAscending:
                return sortKey(left->title) == sortKey(right->title) ? lhs < rhs : sortKey(left->title) < sortKey(right->title);
            case SongSortMode::TitleDescending:
                return sortKey(left->title) == sortKey(right->title) ? lhs < rhs : sortKey(left->title) > sortKey(right->title);
            case SongSortMode::ArtistAscending:
                return sortKey(left->artist) == sortKey(right->artist) ? sortKey(left->title) < sortKey(right->title) : sortKey(left->artist) < sortKey(right->artist);
            case SongSortMode::ArtistDescending:
                return sortKey(left->artist) == sortKey(right->artist) ? sortKey(left->title) < sortKey(right->title) : sortKey(left->artist) > sortKey(right->artist);
            case SongSortMode::AddedAscending:
                return left->added_time == right->added_time ? sortKey(left->title) < sortKey(right->title) : left->added_time < right->added_time;
            case SongSortMode::AddedDescending:
                return left->added_time == right->added_time ? sortKey(left->title) < sortKey(right->title) : left->added_time > right->added_time;
            }
            return lhs < rhs;
        });
        break;
    }
}

void LibraryController::setSongsContextAll()
{
    list_context_.songs.mode = SongsMode::All;
    list_context_.songs.label = "SONGS";
    list_context_.songs.track_ids = allSongIdsSorted();
}

void LibraryController::setSongsContextTrackIds(std::string label, std::vector<int> ids)
{
    setSongsContextFiltered(SongsMode::All, std::move(label), std::move(ids));
}

std::optional<std::string> LibraryController::titleOverrideForPage(AppPage page) const
{
    return LibraryNavigationService::titleOverrideForPage(page, list_context_);
}

std::optional<std::vector<std::pair<std::string, std::string>>> LibraryController::rowsForPage(AppPage page) const
{
    return LibraryNavigationService::rowsForPage(page, repository_.model(), list_context_, visibleAlbums(), playlistTrackIds());
}

LibraryOpenResult LibraryController::openSelectedListItem(AppPage page, int selected)
{
    return ::lofibox::application::LibraryOpenActionService{*this}.openSelectedListItem(page, selected);
}
void LibraryController::setSongsContextAlbum(const AlbumRecord& album)
{
    list_context_.songs.mode = SongsMode::Album;
    list_context_.songs.label = album.album;
    list_context_.songs.track_ids = album.track_ids;
    std::sort(list_context_.songs.track_ids.begin(), list_context_.songs.track_ids.end(), [this](int lhs, int rhs) {
        const auto* left = findTrack(lhs);
        const auto* right = findTrack(rhs);
        return left && right ? left->title < right->title : lhs < rhs;
    });
}

void LibraryController::setSongsContextFiltered(SongsMode mode, std::string label, std::vector<int> ids)
{
    list_context_.songs.mode = mode;
    list_context_.songs.label = std::move(label);
    list_context_.songs.track_ids = std::move(ids);
    std::sort(list_context_.songs.track_ids.begin(), list_context_.songs.track_ids.end(), [this](int lhs, int rhs) {
        const auto* left = findTrack(lhs);
        const auto* right = findTrack(rhs);
        return left && right ? left->title < right->title : lhs < rhs;
    });
}

std::vector<AlbumRecord> LibraryController::visibleAlbums() const
{
    return ::lofibox::application::LibraryQueryService::visibleAlbums(repository_.model(), list_context_.albums);
}

std::vector<int> LibraryController::idsForGenre(const std::string& genre) const
{
    return ::lofibox::application::LibraryQueryService::idsForGenre(repository_.model(), genre);
}

std::vector<int> LibraryController::idsForComposer(const std::string& composer) const
{
    return ::lofibox::application::LibraryQueryService::idsForComposer(repository_.model(), composer);
}

} // namespace lofibox::app
