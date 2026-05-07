// SPDX-License-Identifier: GPL-3.0-or-later
//
// Concrete LibraryEnrichProvider that fetches artist bios and album
// descriptions from Wikipedia via curl, with local caching through
// CacheManager (Metadata bucket).
//
// Included directly by device_main.cpp / x11_main.cpp — the target
// already links lofibox_zero_host_runtime, so curl helpers are available.

#pragma once

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <utility>

#include "app/library_enrich_provider.h"
#include "cache/cache_manager.h"
#include "platform/host/runtime_enrichment_client_helpers.h"
#include "platform/host/runtime_host_internal.h"

namespace lofibox::platform::host {
namespace rt = ::lofibox::platform::host::runtime_detail;

namespace {
namespace fs = std::filesystem;

// --- minimal Wikipedia extract fetcher ---

constexpr std::string_view kWikipediaApiBase =
    "https://en.wikipedia.org/w/api.php"
    "?action=query&prop=extracts&exintro=1&explaintext=1&format=json";

std::string wikipediaUrlForTitle(std::string_view title)
{
    std::string url;
    url.reserve(kWikipediaApiBase.size() + 32 + title.size());
    url += kWikipediaApiBase;
    url += "&titles=";
    // replace spaces with underscores for Wikipedia page titles
    for (char ch : title) {
        url += (ch == ' ') ? '_' : ch;
    }
    return url;
}

// Extract the first page extract from a Wikipedia JSON response.
// Very simple parser: finds "extract":" and reads until the next unescaped ".
std::optional<std::string> parseWikipediaExtract(std::string_view json)
{
    constexpr std::string_view marker = "\"extract\":\"";
    auto pos = json.find(marker);
    if (pos == std::string_view::npos) return std::nullopt;

    pos += marker.size();
    std::string result;
    result.reserve(512);
    while (pos < json.size()) {
        char ch = json[pos];
        if (ch == '\\' && pos + 1 < json.size()) {
            char next = json[++pos];
            switch (next) {
            case 'n':  result += '\n'; break;
            case 't':  result += '\t'; break;
            case '"':  result += '"';  break;
            case '\\': result += '\\'; break;
            default:   result += next; break;
            }
        } else if (ch == '"') {
            break;
        } else {
            result += ch;
        }
        ++pos;
    }
    if (result.empty()) return std::nullopt;
    return result;
}

// Rate-limit helper — wait at least 200ms between Wikipedia requests.
std::chrono::steady_clock::time_point g_last_wikipedia_request{};

void respectWikipediaRateLimit()
{
    using clock = std::chrono::steady_clock;
    constexpr auto kMinInterval = std::chrono::milliseconds(200);
    const auto now = clock::now();
    if (g_last_wikipedia_request != clock::time_point{}) {
        const auto elapsed = now - g_last_wikipedia_request;
        if (elapsed < kMinInterval) {
            std::this_thread::sleep_for(kMinInterval - elapsed);
        }
    }
    g_last_wikipedia_request = clock::now();
}

} // namespace

class LibraryEnrichProviderAdapter final : public app::LibraryEnrichProvider {
public:
    LibraryEnrichProviderAdapter(
            std::shared_ptr<lofibox::cache::CacheManager> cache) noexcept
        : cache_(std::move(cache))
    {
#if defined(_WIN32)
        curl_path_ = rt::resolveExecutablePath(L"CURL_PATH", L"curl.exe");
#elif defined(__linux__)
        curl_path_ = rt::resolveExecutablePath("CURL_PATH", "curl");
#endif
    }

    [[nodiscard]] std::optional<app::ArtistEnrichInfo>
        getArtistInfo(std::string_view artist_name) const override
    {
        if (!curl_path_.has_value()) return std::nullopt;

        // Check cache
        const auto cache_key = std::string{"enrich:artist:"} + std::string{artist_name};
        if (auto cached = cache_->getText(cache::CacheBucket::Metadata, cache_key)) {
            app::ArtistEnrichInfo info{};
            info.summary = std::move(*cached);
            info.source = "wikipedia(cached)";
            return info;
        }

        // Wikipedia lookup
        respectWikipediaRateLimit();
        const auto url = wikipediaUrlForTitle(artist_name);
        const auto json = rt::captureUrl(curl_path_, url);
        if (!json.has_value()) return std::nullopt;

        auto extract = parseWikipediaExtract(*json);
        if (!extract.has_value()) return std::nullopt;

        // Cache the result (30-day TTL — artist bios don't change rapidly)
        cache_->putText(cache::CacheBucket::Metadata, cache_key, *extract,
                        std::chrono::hours{24 * 30});

        app::ArtistEnrichInfo info{};
        info.summary = std::move(*extract);
        info.source = "wikipedia";
        return info;
    }

    [[nodiscard]] std::optional<app::AlbumEnrichInfo>
        getAlbumInfo(std::string_view album_name,
                     std::string_view artist_name) const override
    {
        if (!curl_path_.has_value()) return std::nullopt;

        // Cache key: "enrich:album:" + album + ":" + artist
        const auto cache_key = std::string{"enrich:album:"}
                             + std::string{album_name} + ":"
                             + std::string{artist_name};
        if (auto cached = cache_->getText(cache::CacheBucket::Metadata, cache_key)) {
            app::AlbumEnrichInfo info{};
            info.summary = std::move(*cached);
            info.source = "wikipedia(cached)";
            return info;
        }

        // Wikipedia page title: "Album Name (Artist Name album)"
        std::string title;
        title.reserve(album_name.size() + artist_name.size() + 16);
        title += album_name;
        title += " (";
        title += artist_name;
        title += " album)";

        respectWikipediaRateLimit();
        const auto url = wikipediaUrlForTitle(title);
        const auto json = rt::captureUrl(curl_path_, url);
        if (!json.has_value()) return std::nullopt;

        auto extract = parseWikipediaExtract(*json);
        if (!extract.has_value()) return std::nullopt;

        cache_->putText(cache::CacheBucket::Metadata, cache_key, *extract,
                        std::chrono::hours{24 * 30});

        app::AlbumEnrichInfo info{};
        info.summary = std::move(*extract);
        info.source = "wikipedia";
        return info;
    }

private:
    std::shared_ptr<lofibox::cache::CacheManager> cache_;
    std::optional<std::filesystem::path> curl_path_{};
};

} // namespace lofibox::platform::host
