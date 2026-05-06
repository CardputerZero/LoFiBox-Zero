// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <filesystem>
#include <optional>
#include <string>
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
    [[nodiscard]] LibraryIndexState state() const noexcept;
    [[nodiscard]] const LibraryModel& model() const noexcept;
    [[nodiscard]] LibraryModel& mutableModel() noexcept;

    void startLoading() noexcept;
    void refreshLibrary(const std::vector<std::filesystem::path>& media_roots, const MetadataProvider& metadata_provider);
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

    LibraryRepository repository_{};
    LibraryListContext list_context_{};
};

} // namespace lofibox::app
