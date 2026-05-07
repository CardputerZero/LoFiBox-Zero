// SPDX-License-Identifier: GPL-3.0-or-later

#include "application/library_query_service.h"

#include <algorithm>
#include <cctype>

#include "app/library_controller.h"
#include "app/media_search_service.h"

namespace lofibox::application {
namespace {

std::string sortKey(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

bool trackLess(const app::TrackRecord& left, const app::TrackRecord& right, int lhs, int rhs, app::SongSortMode sort_mode)
{
    switch (sort_mode) {
    case app::SongSortMode::TitleAscending:
        return sortKey(left.title) == sortKey(right.title) ? lhs < rhs : sortKey(left.title) < sortKey(right.title);
    case app::SongSortMode::TitleDescending:
        return sortKey(left.title) == sortKey(right.title) ? lhs < rhs : sortKey(left.title) > sortKey(right.title);
    case app::SongSortMode::ArtistAscending:
        return sortKey(left.artist) == sortKey(right.artist) ? sortKey(left.title) < sortKey(right.title) : sortKey(left.artist) < sortKey(right.artist);
    case app::SongSortMode::ArtistDescending:
        return sortKey(left.artist) == sortKey(right.artist) ? sortKey(left.title) < sortKey(right.title) : sortKey(left.artist) > sortKey(right.artist);
    case app::SongSortMode::AddedAscending:
        return left.added_time == right.added_time ? sortKey(left.title) < sortKey(right.title) : left.added_time < right.added_time;
    case app::SongSortMode::AddedDescending:
        return left.added_time == right.added_time ? sortKey(left.title) < sortKey(right.title) : left.added_time > right.added_time;
    }
    return lhs < rhs;
}

} // namespace

LibraryQueryService::LibraryQueryService(const app::LibraryController& controller) noexcept
    : controller_(controller)
{
}

app::LibraryIndexState LibraryQueryService::state() const noexcept
{
    return controller_.state();
}

const app::LibraryModel& LibraryQueryService::model() const noexcept
{
    return controller_.model();
}

app::LibraryScanProgress LibraryQueryService::scanProgress() const
{
    return controller_.scanProgress();
}

const app::TrackRecord* LibraryQueryService::findTrack(int id) const noexcept
{
    return controller_.findTrack(id);
}

std::vector<int> LibraryQueryService::allSongIdsSorted() const
{
    return controller_.allSongIdsSorted();
}

std::vector<int> LibraryQueryService::trackIdsForCurrentSongs() const
{
    return controller_.trackIdsForCurrentSongs();
}

std::vector<int> LibraryQueryService::playlistTrackIds() const
{
    return controller_.playlistTrackIds();
}

app::SongSortMode LibraryQueryService::songSortMode() const noexcept
{
    return controller_.songSortMode();
}

std::string LibraryQueryService::songSortLabel() const
{
    return controller_.songSortLabel();
}

std::optional<std::string> LibraryQueryService::titleOverrideForPage(app::AppPage page) const
{
    return controller_.titleOverrideForPage(page);
}

std::optional<std::vector<std::pair<std::string, std::string>>> LibraryQueryService::rowsForPage(app::AppPage page) const
{
    return controller_.rowsForPage(page);
}

std::vector<app::MediaItem> LibraryQueryService::searchLocal(std::string_view query, int limit) const
{
    return app::MediaSearchService::searchLocal(controller_.model(), query, limit).local_items;
}

const app::TrackRecord* LibraryQueryService::findTrack(const app::LibraryModel& library, int id) noexcept
{
    for (const auto& track : library.tracks) {
        if (track.id == id) {
            return &track;
        }
    }
    return nullptr;
}

app::TrackRecord* LibraryQueryService::findMutableTrack(app::LibraryModel& library, int id) noexcept
{
    for (auto& track : library.tracks) {
        if (track.id == id) {
            return &track;
        }
    }
    return nullptr;
}

std::vector<int> LibraryQueryService::sortedTrackIds(const app::LibraryModel& library, app::SongSortMode sort_mode)
{
    std::vector<int> ids{};
    ids.reserve(library.tracks.size());
    for (const auto& track : library.tracks) {
        ids.push_back(track.id);
    }
    std::sort(ids.begin(), ids.end(), [&](int lhs, int rhs) {
        const auto* left = findTrack(library, lhs);
        const auto* right = findTrack(library, rhs);
        if (!left || !right) {
            return lhs < rhs;
        }
        return trackLess(*left, *right, lhs, rhs, sort_mode);
    });
    return ids;
}

std::vector<int> LibraryQueryService::idsForGenre(const app::LibraryModel& library, const std::string& genre)
{
    std::vector<int> ids{};
    for (const auto& track : library.tracks) {
        if (track.genre == genre) {
            ids.push_back(track.id);
        }
    }
    return ids;
}

std::vector<int> LibraryQueryService::idsForComposer(const app::LibraryModel& library, const std::string& composer)
{
    std::vector<int> ids{};
    for (const auto& track : library.tracks) {
        if (track.composer == composer) {
            ids.push_back(track.id);
        }
    }
    return ids;
}

std::vector<app::AlbumRecord> LibraryQueryService::visibleAlbums(const app::LibraryModel& library, const app::AlbumsContext& context)
{
    std::vector<app::AlbumRecord> visible{};
    for (const auto& album : library.albums) {
        if (context.mode != app::AlbumsMode::ByArtist || album.artist == context.artist) {
            visible.push_back(album);
        }
    }
    return visible;
}

} // namespace lofibox::application
