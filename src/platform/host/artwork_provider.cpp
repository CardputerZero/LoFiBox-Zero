// SPDX-License-Identifier: GPL-3.0-or-later

#include "platform/host/runtime_provider_factories.h"

#include <algorithm>
#include <array>
#include <bit>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>
#include <cstdint>
#include "platform/host/png_canvas_loader.h"
#include "platform/host/runtime_enrichment_client_helpers.h"
#include "platform/host/runtime_enrichment_clients.h"
#include "platform/host/runtime_logger.h"

namespace fs = std::filesystem;

namespace lofibox::platform::host {
namespace {
using namespace runtime_detail;

std::optional<fs::path> resolveCurlExecutable()
{
#if defined(_WIN32)
    return resolveExecutablePath(L"CURL_PATH", L"curl.exe");
#else
    return resolveExecutablePath("CURL_PATH", "curl");
#endif
}

class FfmpegArtworkProvider final : public app::ArtworkProvider {
public:
    explicit FfmpegArtworkProvider(
        std::shared_ptr<SharedRuntimeCache> cache,
        std::shared_ptr<app::ConnectivityProvider> connectivity,
        std::shared_ptr<app::TagWriter> tag_writer)
        : cache_(std::move(cache))
        , connectivity_(std::move(connectivity))
        , tag_writer_(std::move(tag_writer))
        , curl_path_(resolveCurlExecutable())
        , media_roots_(configuredMediaRoots())
    {
    }

    [[nodiscard]] bool available() const override
    {
        return extractor_.available();
    }

    [[nodiscard]] std::string displayName() const override
    {
        return "BUILT-IN";
    }

    [[nodiscard]] std::optional<core::Canvas> read(const fs::path& path, app::ArtworkReadMode mode = app::ArtworkReadMode::AllowOnline) const override
    {
        if (!cache_) {
            return std::nullopt;
        }

        const auto key = cacheKeyForPath(path);
        const auto cache_path = artworkCachePath(*cache_, key);
        const auto missing_marker = artworkMissingMarkerPath(*cache_, key);
        const bool parent_is_library_root = pathEquivalentToAnyRoot(path.parent_path(), media_roots_);
        std::error_code ec{};
        auto& entry = cache_->entries[key];
        if (!entry.online_metadata_attempted && fs::exists(metadataCachePath(*cache_, key), ec)) {
            entry = loadMetadataCache(*cache_, key);
        }
        const bool allow_online_artwork = mode == app::ArtworkReadMode::AllowOnline;
        const bool can_try_online_artwork = allow_online_artwork
            && (!entry.release_mbid.empty() || !entry.release_group_mbid.empty())
            && !entry.online_artwork_attempted
            && online_artwork_.available()
            && connectivity_
            && connectivity_->connected();

        if (fs::exists(cache_path, ec) && !ec) {
            if (parent_is_library_root && isContaminatedByRootArtwork(path.parent_path(), cache_path)) {
                fs::remove(cache_path, ec);
                fs::remove(missing_marker, ec);
                logRuntime(RuntimeLogLevel::Warn, "artwork", "Discarded cached root-level artwork for " + pathUtf8String(path));
            } else {
            logRuntime(RuntimeLogLevel::Debug, "artwork", "Artwork cache hit for " + pathUtf8String(path));
            return loadPngCanvas(cache_path);
            }
        }
        ec.clear();
        if (fs::exists(missing_marker, ec) && !ec && !can_try_online_artwork) {
            return std::nullopt;
        }

        if (!parent_is_library_root) {
            if (const auto directory_cover = tryDirectoryArtwork(path.parent_path(), cache_path)) {
            logRuntime(RuntimeLogLevel::Info, "artwork", "Directory artwork hit for " + pathUtf8String(path));
            return directory_cover;
        }
        }

        if (const auto embedded = tryEmbeddedArtwork(path, cache_path)) {
            if (parent_is_library_root && isContaminatedByRootArtwork(path.parent_path(), cache_path)) {
                fs::remove(cache_path, ec);
                logRuntime(RuntimeLogLevel::Warn, "artwork", "Ignored embedded artwork matching root-level generic cover for " + pathUtf8String(path));
            } else {
            logRuntime(RuntimeLogLevel::Info, "artwork", "Embedded artwork hit for " + pathUtf8String(path));
            return embedded;
            }
        }

        if (can_try_online_artwork) {
            bool online_hit = false;
            if (!entry.release_mbid.empty()) {
                online_hit = online_artwork_.fetchFrontCover(entry.release_mbid, cache_path);
            }
            if (!online_hit && !entry.release_group_mbid.empty()) {
                online_hit = online_artwork_.fetchReleaseGroupFrontCover(entry.release_group_mbid, cache_path);
            }
            if (online_hit) {
                entry.online_artwork_attempted = true;
                storeMetadataCache(*cache_, key, entry);
                fs::remove(missing_marker, ec);
                writeArtworkBack(path, cache_path, !parent_is_library_root);
                logRuntime(RuntimeLogLevel::Info, "artwork", "Online artwork hit for " + pathUtf8String(path));
                return loadPngCanvas(cache_path);
            }
            entry.online_artwork_attempted = true;
            storeMetadataCache(*cache_, key, entry);
            logRuntime(RuntimeLogLevel::Warn, "artwork", "Online artwork miss for " + pathUtf8String(path));
        } else if (!allow_online_artwork) {
            logRuntime(RuntimeLogLevel::Debug, "artwork", "Online artwork skipped for " + pathUtf8String(path) + ": local-only read");
        } else if (entry.release_mbid.empty() && entry.release_group_mbid.empty()) {
            logRuntime(RuntimeLogLevel::Debug, "artwork", "Online artwork skipped for " + pathUtf8String(path) + ": missing MusicBrainz release identifiers");
        } else if (!online_artwork_.available()) {
            logRuntime(RuntimeLogLevel::Warn, "artwork", "Online artwork skipped for " + pathUtf8String(path) + ": curl unavailable");
        } else if (!connectivity_ || !connectivity_->connected()) {
            logRuntime(RuntimeLogLevel::Warn, "artwork", "Online artwork skipped for " + pathUtf8String(path) + ": network disconnected");
        }

        std::ofstream marker(missing_marker, std::ios::binary | std::ios::trunc);
        marker << "missing";
        logRuntime(RuntimeLogLevel::Debug, "artwork", "No artwork found for " + pathUtf8String(path));
        return std::nullopt;
    }

    [[nodiscard]] std::optional<core::Canvas> readRemoteIdentity(
        std::string_view stable_cache_key,
        const fs::path& lookup_path,
        app::ArtworkReadMode mode = app::ArtworkReadMode::AllowOnline,
        std::string_view remote_artwork_url = {}) const override
    {
        if (!cache_ || stable_cache_key.empty()) {
            return std::nullopt;
        }

        const std::string key{stable_cache_key};
        const auto cache_path = artworkCachePath(*cache_, key);
        const auto missing_marker = artworkMissingMarkerPath(*cache_, key);
        std::error_code ec{};
        auto& entry = cache_->entries[key];
        if (!entry.online_metadata_attempted && fs::exists(metadataCachePath(*cache_, key), ec)) {
            entry = loadMetadataCache(*cache_, key);
        }
        if (fs::exists(cache_path, ec) && !ec) {
            logRuntime(RuntimeLogLevel::Debug, "artwork", "Remote artwork cache hit for " + pathUtf8String(lookup_path));
            return loadPngCanvas(cache_path);
        }
        ec.clear();
        const bool allow_online_artwork = mode == app::ArtworkReadMode::AllowOnline;
        const bool can_try_remote_artwork = allow_online_artwork
            && !remote_artwork_url.empty()
            && connectivity_
            && connectivity_->connected();
        const bool can_try_online_artwork = allow_online_artwork
            && (!entry.release_mbid.empty() || !entry.release_group_mbid.empty())
            && !entry.online_artwork_attempted
            && online_artwork_.available()
            && connectivity_
            && connectivity_->connected();
        if (fs::exists(missing_marker, ec) && !ec && !can_try_remote_artwork && !can_try_online_artwork) {
            return std::nullopt;
        }
        if (can_try_remote_artwork
            && downloadUrlToPngFile(curl_path_, extractor_, std::string{remote_artwork_url}, cache_path)) {
            fs::remove(missing_marker, ec);
            logRuntime(RuntimeLogLevel::Info, "artwork", "Remote provider artwork hit for " + pathUtf8String(lookup_path));
            return loadPngCanvas(cache_path);
        }
        if (can_try_online_artwork) {
            bool online_hit = false;
            if (!entry.release_mbid.empty()) {
                online_hit = online_artwork_.fetchFrontCover(entry.release_mbid, cache_path);
            }
            if (!online_hit && !entry.release_group_mbid.empty()) {
                online_hit = online_artwork_.fetchReleaseGroupFrontCover(entry.release_group_mbid, cache_path);
            }
            entry.online_artwork_attempted = true;
            storeMetadataCache(*cache_, key, entry);
            if (online_hit) {
                fs::remove(missing_marker, ec);
                logRuntime(RuntimeLogLevel::Info, "artwork", "Remote online artwork hit for " + pathUtf8String(lookup_path));
                return loadPngCanvas(cache_path);
            }
            logRuntime(RuntimeLogLevel::Warn, "artwork", "Remote online artwork miss for " + pathUtf8String(lookup_path));
        }
        std::ofstream marker(missing_marker, std::ios::binary | std::ios::trunc);
        marker << "missing";
        return std::nullopt;
    }

private:
    void writeArtworkBack(const fs::path& target_path, const fs::path& cache_path, bool only_fill_missing) const
    {
        if (!tag_writer_ || !tag_writer_->available()) {
            return;
        }
        app::TagWriteRequest request{};
        request.artwork_png_path = cache_path;
        request.only_fill_missing = only_fill_missing;
        if (tag_writer_->write(target_path, request)) {
            logRuntime(RuntimeLogLevel::Info, "artwork", "Wrote artwork back to file: " + pathUtf8String(target_path));
        } else {
            logRuntime(RuntimeLogLevel::Warn, "artwork", "Failed to write artwork back to file: " + pathUtf8String(target_path));
        }
    }

    [[nodiscard]] bool isContaminatedByRootArtwork(const fs::path& directory, const fs::path& artwork_path) const
    {
        if (!pathEquivalentToAnyRoot(directory, media_roots_)) {
            return false;
        }

        const auto actual = loadPngCanvas(artwork_path);
        if (!actual) {
            return false;
        }

        for (const auto& candidate : existingDirectoryArtworkCandidates(directory)) {
            if (const auto candidate_canvas = loadPngCanvas(candidate)) {
                if (sameCanvas(*actual, *candidate_canvas)) {
                    return true;
                }
            } else {
                std::error_code ec{};
                const fs::path temp_path = cache_->root / "artwork" / ("root_art_" + cacheKeyForPath(candidate) + ".png");
                if (auto materialized = extractor_.materialize(candidate, temp_path)) {
                    if (sameCanvas(*actual, *materialized)) {
                        fs::remove(temp_path, ec);
                        return true;
                    }
                    fs::remove(temp_path, ec);
                }
            }
        }
        return false;
    }

    [[nodiscard]] static std::uint64_t averageHash(const core::Canvas& canvas)
    {
        if (canvas.width() <= 0 || canvas.height() <= 0) {
            return 0;
        }

        std::array<int, 64> samples{};
        int sum = 0;
        int index = 0;
        for (int y = 0; y < 8; ++y) {
            const int source_y = std::min(canvas.height() - 1, (y * canvas.height()) / 8);
            for (int x = 0; x < 8; ++x) {
                const int source_x = std::min(canvas.width() - 1, (x * canvas.width()) / 8);
                const auto pixel = canvas.pixel(source_x, source_y);
                const int luminance = (static_cast<int>(pixel.r) * 299 + static_cast<int>(pixel.g) * 587 + static_cast<int>(pixel.b) * 114) / 1000;
                samples[static_cast<std::size_t>(index++)] = luminance;
                sum += luminance;
            }
        }

        const int average = sum / 64;
        std::uint64_t hash = 0;
        for (std::size_t i = 0; i < samples.size(); ++i) {
            if (samples[i] >= average) {
                hash |= (std::uint64_t{1} << i);
            }
        }
        return hash;
    }

    [[nodiscard]] static bool sameCanvas(const core::Canvas& lhs, const core::Canvas& rhs)
    {
        if (lhs.width() == rhs.width()
            && lhs.height() == rhs.height()
            && lhs.pixels() == rhs.pixels()) {
            return true;
        }

        const auto lhs_hash = averageHash(lhs);
        const auto rhs_hash = averageHash(rhs);
        const auto distance = static_cast<unsigned int>(std::popcount(lhs_hash ^ rhs_hash));
        return distance <= 6;
    }

    [[nodiscard]] std::optional<core::Canvas> tryEmbeddedArtwork(const fs::path& path, const fs::path& cache_path) const
    {
        return extractor_.extractEmbedded(path, cache_path);
    }

    [[nodiscard]] std::optional<core::Canvas> tryDirectoryArtwork(const fs::path& directory, const fs::path& cache_path) const
    {
        for (const auto& candidate : existingDirectoryArtworkCandidates(directory)) {
            if (auto canvas = extractor_.materialize(candidate, cache_path)) {
                return canvas;
            }
        }
        return std::nullopt;
    }

    [[nodiscard]] static std::vector<fs::path> existingDirectoryArtworkCandidates(const fs::path& directory)
    {
        static const std::array<std::string_view, 12> candidates{
            "cover.jpg", "cover.jpeg", "cover.png",
            "Cover.jpg", "Cover.jpeg", "Cover.png",
            "folder.jpg", "folder.jpeg", "folder.png",
            "Folder.jpg", "Folder.jpeg", "Folder.png"};
        static const std::array<std::string_view, 8> extra{
            "front.jpg", "front.png", "Front.jpg", "Front.png",
            "AlbumArtSmall.jpg", "AlbumArtSmall.png", "albumartsmall.jpg", "albumartsmall.png"};

        std::vector<fs::path> matches{};
        for (const auto& name : candidates) {
            std::error_code ec{};
            const fs::path candidate = directory / std::string(name);
            if (fs::exists(candidate, ec) && !ec) {
                matches.push_back(candidate);
            }
        }
        for (const auto& name : extra) {
            std::error_code ec{};
            const fs::path candidate = directory / std::string(name);
            if (fs::exists(candidate, ec) && !ec) {
                matches.push_back(candidate);
            }
        }
        return matches;
    }

    std::shared_ptr<SharedRuntimeCache> cache_{};
    std::shared_ptr<app::ConnectivityProvider> connectivity_{};
    std::shared_ptr<app::TagWriter> tag_writer_{};
    std::optional<fs::path> curl_path_{};
    FfmpegArtworkExtractor extractor_{};
    CoverArtArchiveClient online_artwork_{};
    std::vector<fs::path> media_roots_{};
};

} // namespace

std::shared_ptr<app::ArtworkProvider> createHostArtworkProvider(std::shared_ptr<runtime_detail::SharedRuntimeCache> cache, std::shared_ptr<app::ConnectivityProvider> connectivity, std::shared_ptr<app::TagWriter> tag_writer)
{
    return std::make_shared<FfmpegArtworkProvider>(std::move(cache), std::move(connectivity), std::move(tag_writer));
}

} // namespace lofibox::platform::host
