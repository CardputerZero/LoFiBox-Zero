// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <atomic>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "app/app_page.h"
#include "app/library_list_context.h"
#include "app/library_model.h"
#include "app/library_repository.h"
#include "app/remote_media_services.h"
#include "app/runtime_services.h"

namespace lofibox::application {
class LibraryOpenActionService;
}
namespace lofibox::runtime {
class RuntimeSessionFacade;
}

namespace lofibox::app {

struct LibraryOpenResult {
    enum class Kind {
        None,
        PushPage,
        StartTrack,
    };

    Kind kind{Kind::None};
    AppPage page{AppPage::Boot};
    int track_id{0};
};

class LibraryController {
public:
    LibraryController() = default;
    ~LibraryController();
    LibraryController(const LibraryController&) = delete;
    LibraryController& operator=(const LibraryController&) = delete;

    [[nodiscard]] LibraryIndexState state() const noexcept;
    [[nodiscard]] const LibraryModel& model() const noexcept;
    [[nodiscard]] LibraryModel& mutableModel() noexcept;

    void startLoading() noexcept;
    void refreshLibrary(const std::vector<std::filesystem::path>& media_roots, const MetadataProvider& metadata_provider);
    void beginAsyncRefreshLibrary(
        const std::vector<std::filesystem::path>& media_roots,
        std::shared_ptr<MetadataProvider> metadata_provider);
    [[nodiscard]] bool pollAsyncRefreshLibrary();
    [[nodiscard]] bool asyncRefreshRunning() const noexcept;
    [[nodiscard]] LibraryScanProgress scanProgress() const;
    void mergeRemoteTracks(const RemoteServerProfile& profile, const std::vector<RemoteTrack>& tracks);
    bool applyRemoteTrackFacts(const RemoteServerProfile& profile, const RemoteTrack& remote_track);

    [[nodiscard]] const TrackRecord* findTrack(int id) const noexcept;
    [[nodiscard]] TrackRecord* findMutableTrack(int id) noexcept;
    [[nodiscard]] std::vector<int> allSongIdsSorted() const;
    [[nodiscard]] std::vector<int> trackIdsForCurrentSongs() const;
    [[nodiscard]] std::vector<int> playlistTrackIds() const;
    [[nodiscard]] SongSortMode songSortMode() const noexcept;
    [[nodiscard]] std::string songSortLabel() const;
    void cycleSongSortMode();

    void setSongsContextAll();
    void setSongsContextTrackIds(std::string label, std::vector<int> ids);
    [[nodiscard]] std::optional<std::string> titleOverrideForPage(AppPage page) const;
    [[nodiscard]] std::optional<std::vector<std::pair<std::string, std::string>>> rowsForPage(AppPage page) const;
    [[nodiscard]] LibraryOpenResult openSelectedListItem(AppPage page, int selected);

    void setSongsContextAlbum(const AlbumRecord& album);
    void setSongsContextFiltered(SongsMode mode, std::string label, std::vector<int> ids);
    [[nodiscard]] std::vector<AlbumRecord> visibleAlbums() const;
    [[nodiscard]] std::vector<int> idsForGenre(const std::string& genre) const;
    [[nodiscard]] std::vector<int> idsForComposer(const std::string& composer) const;

private:
    friend class ::lofibox::application::LibraryOpenActionService;
    friend class ::lofibox::runtime::RuntimeSessionFacade;

    void publishScanProgress(const LibraryScanProgress& progress);
    void resetScanProgress(LibraryScanPhase phase, std::string message);
    void joinAsyncScan() noexcept;

    LibraryRepository repository_{};
    LibraryListContext list_context_{};
    std::thread scan_thread_{};
    mutable std::mutex async_mutex_{};
    std::optional<LibraryModel> pending_scan_model_{};
    std::string pending_scan_error_{};
    std::atomic<bool> scan_running_{false};
    std::atomic<bool> scan_done_{false};
    std::atomic<int> scan_phase_{static_cast<int>(LibraryScanPhase::Idle)};
    std::atomic<int> scan_roots_total_{0};
    std::atomic<int> scan_roots_scanned_{0};
    std::atomic<int> scan_files_discovered_{0};
    std::atomic<int> scan_files_total_{0};
    std::atomic<int> scan_files_processed_{0};
    std::atomic<int> scan_tracks_indexed_{0};
    std::atomic<bool> scan_degraded_{false};
    mutable std::mutex scan_progress_mutex_{};
    std::string scan_current_path_{};
    std::string scan_message_{};
};

} // namespace lofibox::app
