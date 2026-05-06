// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "app/app_page.h"
#include "app/library_model.h"
#include "app/media_item.h"

namespace lofibox::app {
class LibraryController;
}

namespace lofibox::application {

class LibraryQueryService {
public:
    explicit LibraryQueryService(const app::LibraryController& controller) noexcept;

    [[nodiscard]] app::LibraryIndexState state() const noexcept;
    [[nodiscard]] const app::LibraryModel& model() const noexcept;
    [[nodiscard]] app::LibraryScanProgress scanProgress() const;
    [[nodiscard]] const app::TrackRecord* findTrack(int id) const noexcept;
    [[nodiscard]] std::vector<int> allSongIdsSorted() const;
    [[nodiscard]] std::vector<int> trackIdsForCurrentSongs() const;
    [[nodiscard]] std::vector<int> playlistTrackIds() const;
    [[nodiscard]] app::SongSortMode songSortMode() const noexcept;
    [[nodiscard]] std::string songSortLabel() const;
    [[nodiscard]] std::optional<std::string> titleOverrideForPage(app::AppPage page) const;
    [[nodiscard]] std::optional<std::vector<std::pair<std::string, std::string>>> rowsForPage(app::AppPage page) const;
    [[nodiscard]] std::vector<app::MediaItem> searchLocal(std::string_view query, int limit) const;

    [[nodiscard]] static const app::TrackRecord* findTrack(const app::LibraryModel& library, int id) noexcept;
    [[nodiscard]] static app::TrackRecord* findMutableTrack(app::LibraryModel& library, int id) noexcept;
    [[nodiscard]] static std::vector<int> sortedTrackIds(const app::LibraryModel& library, app::SongSortMode sort_mode);
    [[nodiscard]] static std::vector<int> idsForGenre(const app::LibraryModel& library, const std::string& genre);
    [[nodiscard]] static std::vector<int> idsForComposer(const app::LibraryModel& library, const std::string& composer);
    [[nodiscard]] static std::vector<app::AlbumRecord> visibleAlbums(const app::LibraryModel& library, const app::AlbumsContext& context);

private:
    const app::LibraryController& controller_;
};

} // namespace lofibox::application
