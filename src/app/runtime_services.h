// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <filesystem>
#include <array>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "app/audio_visualization.h"
#include "app/remote_profile_store.h"
#include "app/remote_media_services.h"
#include "cache/cache_manager.h"
#include "core/canvas.h"
#include "ui/ui_theme.h"

namespace lofibox::audio::dsp {
struct DspChainProfile;
}

namespace lofibox::app {

struct TrackMetadata {
    std::optional<std::string> title{};
    std::optional<std::string> artist{};
    std::optional<std::string> album{};
    std::optional<std::string> genre{};
    std::optional<std::string> composer{};
    std::optional<int> duration_seconds{};
};

struct TrackLyrics {
    std::optional<std::string> plain{};
    std::optional<std::string> synced{};
    std::string source{};
};

struct TrackIdentity {
    TrackMetadata metadata{};
    std::string recording_mbid{};
    std::string release_mbid{};
    std::string release_group_mbid{};
    std::string fingerprint{};
    std::string source{};
    double confidence{0.0};
    bool found{false};
    bool audio_fingerprint_verified{false};
};

struct TagWriteRequest {
    std::optional<TrackMetadata> metadata{};
    std::optional<TrackIdentity> identity{};
    std::optional<std::filesystem::path> artwork_png_path{};
    std::optional<TrackLyrics> lyrics{};
    bool only_fill_missing{true};
    bool clear_missing_metadata{false};
};

enum class MetadataReadMode {
    LocalOnly,
    AllowOnline,
};

enum class ArtworkReadMode {
    LocalOnly,
    AllowOnline,
};

class MetadataProvider {
public:
    virtual ~MetadataProvider() = default;

    [[nodiscard]] virtual bool available() const = 0;
    [[nodiscard]] virtual std::string displayName() const = 0;
    [[nodiscard]] virtual TrackMetadata read(const std::filesystem::path& path, MetadataReadMode mode = MetadataReadMode::AllowOnline) const = 0;
};

class TrackIdentityProvider {
public:
    virtual ~TrackIdentityProvider() = default;

    [[nodiscard]] virtual bool available() const = 0;
    [[nodiscard]] virtual std::string displayName() const = 0;
    [[nodiscard]] virtual TrackIdentity resolve(const std::filesystem::path& path, const TrackMetadata& seed_metadata = {}) const = 0;
    [[nodiscard]] virtual TrackIdentity resolveRemoteStream(
        std::string_view stable_cache_key,
        std::string_view stream_uri,
        const std::filesystem::path& lookup_path,
        const TrackMetadata& seed_metadata = {}) const
    {
        (void)stable_cache_key;
        (void)stream_uri;
        return resolve(lookup_path, seed_metadata);
    }
};

class ArtworkProvider {
public:
    virtual ~ArtworkProvider() = default;

    [[nodiscard]] virtual bool available() const = 0;
    [[nodiscard]] virtual std::string displayName() const = 0;
    [[nodiscard]] virtual std::optional<core::Canvas> read(const std::filesystem::path& path, ArtworkReadMode mode = ArtworkReadMode::AllowOnline) const = 0;
    [[nodiscard]] virtual std::optional<core::Canvas> readRemoteIdentity(
        std::string_view stable_cache_key,
        const std::filesystem::path& lookup_path,
        ArtworkReadMode mode = ArtworkReadMode::AllowOnline,
        std::string_view remote_artwork_url = {}) const
    {
        (void)stable_cache_key;
        (void)remote_artwork_url;
        return read(lookup_path, mode);
    }
};

enum class AudioPlaybackState {
    Idle,
    Starting,
    Playing,
    Paused,
    Finished,
    Failed,
};

class AudioPlaybackBackend {
public:
    virtual ~AudioPlaybackBackend() = default;

    [[nodiscard]] virtual bool available() const = 0;
    [[nodiscard]] virtual std::string displayName() const = 0;
    virtual bool playFile(const std::filesystem::path& path, double start_seconds) = 0;
    virtual bool playUri(const std::string& uri, double start_seconds)
    {
        (void)uri;
        (void)start_seconds;
        return false;
    }
    virtual void pause() {}
    virtual void resume() {}
    virtual void stop() = 0;
    virtual void setDspProfile(const ::lofibox::audio::dsp::DspChainProfile& profile) { (void)profile; }
    [[nodiscard]] virtual bool isPlaying() = 0;
    [[nodiscard]] virtual bool isFinished() = 0;
    [[nodiscard]] virtual std::optional<double> positionSeconds() const { return std::nullopt; }
    [[nodiscard]] virtual AudioPlaybackState state()
    {
        if (isFinished()) {
            return AudioPlaybackState::Finished;
        }
        if (isPlaying()) {
            return AudioPlaybackState::Playing;
        }
        return AudioPlaybackState::Idle;
    }
    [[nodiscard]] virtual AudioVisualizationFrame visualizationFrame() const { return {}; }
};

class LyricsProvider {
public:
    virtual ~LyricsProvider() = default;

    [[nodiscard]] virtual bool available() const = 0;
    [[nodiscard]] virtual std::string displayName() const = 0;
    [[nodiscard]] virtual TrackLyrics fetch(const std::filesystem::path& path, const TrackMetadata& metadata) const = 0;
    [[nodiscard]] virtual TrackLyrics fetchRemoteIdentity(
        std::string_view stable_cache_key,
        const std::filesystem::path& lookup_path,
        const TrackMetadata& metadata,
        const TrackIdentity& accepted_identity = {}) const
    {
        (void)stable_cache_key;
        (void)accepted_identity;
        return fetch(lookup_path, metadata);
    }
};

class TagWriter {
public:
    virtual ~TagWriter() = default;

    [[nodiscard]] virtual bool available() const = 0;
    [[nodiscard]] virtual std::string displayName() const = 0;
    virtual bool write(const std::filesystem::path& path, const TagWriteRequest& request) const = 0;
};

class ConnectivityProvider {
public:
    virtual ~ConnectivityProvider() = default;

    [[nodiscard]] virtual bool connected() const = 0;
    [[nodiscard]] virtual std::string displayName() const = 0;
};

struct ConnectivityServices {
    std::shared_ptr<ConnectivityProvider> provider{};
};

struct MetadataServices {
    std::shared_ptr<MetadataProvider> metadata_provider{};
    std::shared_ptr<TrackIdentityProvider> track_identity_provider{};
    std::shared_ptr<ArtworkProvider> artwork_provider{};
    std::shared_ptr<LyricsProvider> lyrics_provider{};
    std::shared_ptr<TagWriter> tag_writer{};
};

struct PlaybackServices {
    std::shared_ptr<AudioPlaybackBackend> audio_backend{};
};

struct RemoteMediaServices {
    std::shared_ptr<RemoteSourceProvider> remote_source_provider{};
    std::shared_ptr<RemoteCatalogProvider> remote_catalog_provider{};
    std::shared_ptr<RemoteStreamResolver> remote_stream_resolver{};
    std::shared_ptr<RemoteProfileStore> remote_profile_store{};
};

struct CacheServices {
    std::shared_ptr<::lofibox::cache::CacheManager> cache_manager{};
};

struct UiServices {
    std::shared_ptr<ui::UiTheme> theme{};
    std::string webui_url{};
};

struct PluginServices {
    std::vector<std::string> loaded_plugin_ids{};
    std::vector<std::string> warnings{};
    std::string selected_skin_id{};
};

struct RuntimeServiceRegistry {
    ConnectivityServices connectivity{};
    MetadataServices metadata{};
    PlaybackServices playback{};
    RemoteMediaServices remote{};
    CacheServices cache{};
    UiServices ui{};
    PluginServices plugins{};
};

using RuntimeServices = RuntimeServiceRegistry;

[[nodiscard]] RuntimeServices withNullRuntimeServices(RuntimeServices services = {});

} // namespace lofibox::app
