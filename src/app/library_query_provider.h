// SPDX-License-Identifier: GPL-3.0-or-later
//
// Abstract interface for library content queries.
// Lives in lofibox_zero_core — lofibox_webui already links it.
// Concrete implementations wrap LibraryController / LibraryQueryService
// and are injected into WebUiHttpRouter at construction time.

#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace lofibox::app {

struct LibraryTrackInfo {
    int id{0};
    std::string title{};
    std::string artist{};
    std::string album{};
    std::string genre{};
    int duration_seconds{0};
    int track_number{0};
    int play_count{0};
};

struct LibraryAlbumInfo {
    std::string name{};
    std::string artist{};
    int track_count{0};
    std::string artwork_url{};   // first track's artwork URL, for thumbnail
    int first_track_id{0};       // for constructing /api/artwork/<id>
};

class LibraryQueryProvider {
public:
    virtual ~LibraryQueryProvider() = default;

    // List all tracks, sorted by the library's default sort order.
    [[nodiscard]] virtual std::vector<LibraryTrackInfo> listTracks() const = 0;

    // List all albums.
    [[nodiscard]] virtual std::vector<LibraryAlbumInfo> listAlbums() const = 0;

    // Return cached PNG artwork bytes for a track, or nullopt if unavailable.
    [[nodiscard]] virtual std::optional<std::vector<std::uint8_t>>
        getArtworkPng(int track_id) const = 0;
};

} // namespace lofibox::app
