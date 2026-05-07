// SPDX-License-Identifier: GPL-3.0-or-later
//
// Abstract interface for supplementary library enrichment data
// (artist biographies, album descriptions).
// Lives in lofibox_zero_core — lofibox_webui already links it.
// Returns std::optional because enrichment data comes from the network
// and may be unavailable when offline or for obscure artists/albums.

#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace lofibox::app {

struct ArtistEnrichInfo {
    std::string summary{};  // plain-text prose (typically a Wikipedia extract)
    std::string source{};   // "wikipedia" / "musicbrainz"
};

struct AlbumEnrichInfo {
    std::string summary{};  // plain-text prose
    std::string source{};
    std::string genres{};   // comma-separated tags
    int year{0};
};

class LibraryEnrichProvider {
public:
    virtual ~LibraryEnrichProvider() = default;

    // Look up enrichment info for an artist by display name.
    // Returns std::nullopt when no data is available (offline / not found).
    [[nodiscard]] virtual std::optional<ArtistEnrichInfo>
        getArtistInfo(std::string_view artist_name) const = 0;

    // Look up enrichment info for an album by name + artist.
    [[nodiscard]] virtual std::optional<AlbumEnrichInfo>
        getAlbumInfo(std::string_view album_name,
                     std::string_view artist_name) const = 0;
};

} // namespace lofibox::app
