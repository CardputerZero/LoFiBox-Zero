// SPDX-License-Identifier: GPL-3.0-or-later
//
// Concrete LibraryQueryProvider that delegates to LibraryQueryService
// and uses the host ArtworkProvider for artwork extraction + read-back.

#pragma once

#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <optional>
#include <vector>

#include "app/library_query_provider.h"
#include "app/runtime_services.h"
#include "application/library_query_service.h"
#include "platform/host/runtime_host_internal.h"

namespace lofibox::application {

class LibraryQueryProviderAdapter final : public app::LibraryQueryProvider {
public:
    LibraryQueryProviderAdapter() noexcept = default;

    LibraryQueryProviderAdapter(
            std::shared_ptr<app::ArtworkProvider> artwork_provider,
            std::filesystem::path artwork_cache_root) noexcept
        : artwork_(std::move(artwork_provider))
        , artwork_cache_root_(std::move(artwork_cache_root))
    {
    }

    void bind(LibraryQueryService&& service) noexcept { service_.emplace(std::move(service)); }

    [[nodiscard]] std::vector<app::LibraryTrackInfo> listTracks() const override
    {
        std::vector<app::LibraryTrackInfo> result;
        if (!service_) return result;
        const auto ids = service_->allSongIdsSorted();
        result.reserve(ids.size());
        for (int id : ids) {
            const auto* track = service_->findTrack(id);
            if (!track) continue;
            result.push_back(app::LibraryTrackInfo{
                .id = track->id,
                .title = track->title,
                .artist = track->artist,
                .album = track->album,
                .genre = track->genre,
                .duration_seconds = track->duration_seconds,
                .play_count = track->play_count,
            });
        }
        return result;
    }

    [[nodiscard]] std::vector<app::LibraryAlbumInfo> listAlbums() const override
    {
        std::vector<app::LibraryAlbumInfo> result;
        if (!service_) return result;
        const auto& model = service_->model();
        result.reserve(model.albums.size());
        for (const auto& album : model.albums) {
            std::string artwork_url;
            int first_track_id = 0;
            // Pick the first track with artwork_url, or just the first track
            for (int tid : album.track_ids) {
                const auto* t = service_->findTrack(tid);
                if (!t) continue;
                if (first_track_id == 0) first_track_id = t->id;
                if (!t->artwork_url.empty()) {
                    artwork_url = t->artwork_url;
                    first_track_id = t->id;
                    break;
                }
            }
            result.push_back(app::LibraryAlbumInfo{
                .name = album.album,
                .artist = album.artist,
                .track_count = static_cast<int>(album.track_ids.size()),
                .artwork_url = std::move(artwork_url),
                .first_track_id = first_track_id,
            });
        }
        return result;
    }

    [[nodiscard]] std::optional<std::vector<std::uint8_t>>
        getArtworkPng(int track_id) const override
    {
        if (!service_ || !artwork_) return std::nullopt;

        const auto* track = service_->findTrack(track_id);
        if (!track) return std::nullopt;

        namespace rd = lofibox::platform::host::runtime_detail;

        // Remote track — artwork_url is a tokenless URL from the remote server.
        // Download on demand via readRemoteIdentity, which caches to
        // appCacheDir()/artwork/<key>.png.
        if (track->remote && !track->artwork_url.empty()) {
            // Try the enrichment-path key first (populated during playback).
            const auto enrich_key = "remote-media-emby-"
                                  + track->remote_profile_id + "-"
                                  + track->remote_track_id;
            auto cache_path = artwork_cache_root_ / "artwork" / (enrich_key + ".png");
            auto cached = readBinaryFile(cache_path);
            if (cached) return cached;

            // Fall back: artwork_url hash key shared across same-album tracks.
            const auto shared_key = "remote-artwork-"
                                  + std::to_string(std::hash<std::string>{}(track->artwork_url));
            cache_path = artwork_cache_root_ / "artwork" / (shared_key + ".png");
            cached = readBinaryFile(cache_path);
            if (cached) return cached;

            // Neither cached — trigger download via readRemoteIdentity.
            // readRemoteIdentity writes a permanent "missing" marker on failure;
            // clean it up so retries aren't permanently blocked.
            if (artwork_->available()) {
                const auto marker_path = artwork_cache_root_ / "artwork" / (shared_key + ".missing");
                std::error_code ec;
                std::filesystem::remove(marker_path, ec);
                artwork_->readRemoteIdentity(
                    shared_key,
                    track->path,
                    app::ArtworkReadMode::AllowOnline,
                    track->artwork_url);
                auto result = readBinaryFile(cache_path);
                if (!result) {
                    std::filesystem::remove(marker_path, ec);
                }
                return result;
            }
            return std::nullopt;
        }

        // Local track — extract embedded artwork via ffmpeg
        if (!artwork_->available()) return std::nullopt;
        const auto key = rd::cacheKeyForPath(track->path);
        const auto cache_path = artwork_cache_root_ / "artwork" / (key + ".png");

        auto cached = readBinaryFile(cache_path);
        if (cached) return cached;

        artwork_->read(track->path);
        return readBinaryFile(cache_path);
    }

private:
    static std::optional<std::vector<std::uint8_t>>
        readBinaryFile(const std::filesystem::path& path)
    {
        std::error_code ec;
        if (!std::filesystem::exists(path, ec) || ec) return std::nullopt;
        std::ifstream file(path, std::ios::binary | std::ios::ate);
        if (!file) return std::nullopt;
        std::vector<std::uint8_t> result(static_cast<std::size_t>(file.tellg()));
        file.seekg(0);
        file.read(reinterpret_cast<char*>(result.data()),
                  static_cast<std::streamsize>(result.size()));
        if (!file) return std::nullopt;
        return result;
    }

    std::shared_ptr<app::ArtworkProvider> artwork_{};
    std::filesystem::path artwork_cache_root_{};
    std::optional<LibraryQueryService> service_{};
};

} // namespace lofibox::application
