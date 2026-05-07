// SPDX-License-Identifier: GPL-3.0-or-later

#include "app/runtime_services.h"

#include <filesystem>

namespace lofibox::app {
namespace {

class NullMetadataProvider final : public MetadataProvider {
public:
    [[nodiscard]] bool available() const override { return false; }
    [[nodiscard]] std::string displayName() const override { return "BUILT-IN"; }
    [[nodiscard]] TrackMetadata read(const std::filesystem::path&, MetadataReadMode = MetadataReadMode::AllowOnline) const override { return {}; }
};

class NullTrackIdentityProvider final : public TrackIdentityProvider {
public:
    [[nodiscard]] bool available() const override { return false; }
    [[nodiscard]] std::string displayName() const override { return "UNAVAILABLE"; }
    [[nodiscard]] TrackIdentity resolve(const std::filesystem::path&, const TrackMetadata& = {}) const override { return {}; }
    [[nodiscard]] TrackIdentity resolveRemoteStream(std::string_view, std::string_view, const std::filesystem::path&, const TrackMetadata& = {}) const override { return {}; }
};

class NullArtworkProvider final : public ArtworkProvider {
public:
    [[nodiscard]] bool available() const override { return false; }
    [[nodiscard]] std::string displayName() const override { return "BUILT-IN"; }
    [[nodiscard]] std::optional<core::Canvas> read(const std::filesystem::path&, ArtworkReadMode = ArtworkReadMode::AllowOnline) const override { return std::nullopt; }
    [[nodiscard]] std::optional<core::Canvas> readRemoteIdentity(std::string_view, const std::filesystem::path&, ArtworkReadMode = ArtworkReadMode::AllowOnline, std::string_view = {}) const override { return std::nullopt; }
};

class NullAudioPlaybackBackend final : public AudioPlaybackBackend {
public:
    [[nodiscard]] bool available() const override { return false; }
    [[nodiscard]] std::string displayName() const override { return "UNAVAILABLE"; }
    bool playFile(const std::filesystem::path&, double) override { return false; }
    bool playUri(const std::string&, double) override { return false; }
    void stop() override {}
    [[nodiscard]] bool isPlaying() override { return false; }
    [[nodiscard]] bool isFinished() override { return false; }
};

class NullLyricsProvider final : public LyricsProvider {
public:
    [[nodiscard]] bool available() const override { return false; }
    [[nodiscard]] std::string displayName() const override { return "UNAVAILABLE"; }
    [[nodiscard]] TrackLyrics fetch(const std::filesystem::path&, const TrackMetadata&) const override { return {}; }
};

class NullTagWriter final : public TagWriter {
public:
    [[nodiscard]] bool available() const override { return false; }
    [[nodiscard]] std::string displayName() const override { return "UNAVAILABLE"; }
    bool write(const std::filesystem::path&, const TagWriteRequest&) const override { return false; }
};

class NullConnectivityProvider final : public ConnectivityProvider {
public:
    [[nodiscard]] bool connected() const override { return false; }
    [[nodiscard]] std::string displayName() const override { return "SYSTEM"; }
};

class NullRemoteSourceProvider final : public RemoteSourceProvider {
public:
    [[nodiscard]] bool available() const override { return false; }
    [[nodiscard]] std::string displayName() const override { return "UNAVAILABLE"; }
    [[nodiscard]] RemoteSourceSession probe(const RemoteServerProfile&) const override { return {}; }
};

class NullRemoteCatalogProvider final : public RemoteCatalogProvider {
public:
    [[nodiscard]] bool available() const override { return false; }
    [[nodiscard]] std::string displayName() const override { return "UNAVAILABLE"; }
    [[nodiscard]] std::vector<RemoteTrack> searchTracks(const RemoteServerProfile&, const RemoteSourceSession&, std::string_view, int) const override { return {}; }
    [[nodiscard]] std::vector<RemoteTrack> recentTracks(const RemoteServerProfile&, const RemoteSourceSession&, int) const override { return {}; }
};

class NullRemoteStreamResolver final : public RemoteStreamResolver {
public:
    [[nodiscard]] bool available() const override { return false; }
    [[nodiscard]] std::string displayName() const override { return "UNAVAILABLE"; }
    [[nodiscard]] std::optional<ResolvedRemoteStream> resolveTrack(const RemoteServerProfile&, const RemoteSourceSession&, const RemoteTrack&) const override { return std::nullopt; }
};

class NullRemoteProfileStore final : public RemoteProfileStore {
public:
    [[nodiscard]] std::vector<RemoteServerProfile> loadProfiles() const override { return {}; }
    bool saveProfiles(const std::vector<RemoteServerProfile>&) const override { return false; }
    bool deleteCredentials(const ::lofibox::security::CredentialRef&) const override { return false; }
};

template <typename T>
std::shared_ptr<T> makeNullShared()
{
    return std::make_shared<T>();
}

} // namespace

RuntimeServices withNullRuntimeServices(RuntimeServices services)
{
    if (!services.connectivity.provider) {
        services.connectivity.provider = makeNullShared<NullConnectivityProvider>();
    }
    if (!services.metadata.metadata_provider) {
        services.metadata.metadata_provider = makeNullShared<NullMetadataProvider>();
    }
    if (!services.metadata.track_identity_provider) {
        services.metadata.track_identity_provider = makeNullShared<NullTrackIdentityProvider>();
    }
    if (!services.metadata.artwork_provider) {
        services.metadata.artwork_provider = makeNullShared<NullArtworkProvider>();
    }
    if (!services.metadata.lyrics_provider) {
        services.metadata.lyrics_provider = makeNullShared<NullLyricsProvider>();
    }
    if (!services.metadata.tag_writer) {
        services.metadata.tag_writer = makeNullShared<NullTagWriter>();
    }
    if (!services.playback.audio_backend) {
        services.playback.audio_backend = makeNullShared<NullAudioPlaybackBackend>();
    }
    if (!services.remote.remote_source_provider) {
        services.remote.remote_source_provider = makeNullShared<NullRemoteSourceProvider>();
    }
    if (!services.remote.remote_catalog_provider) {
        services.remote.remote_catalog_provider = makeNullShared<NullRemoteCatalogProvider>();
    }
    if (!services.remote.remote_stream_resolver) {
        services.remote.remote_stream_resolver = makeNullShared<NullRemoteStreamResolver>();
    }
    if (!services.remote.remote_profile_store) {
        services.remote.remote_profile_store = makeNullShared<NullRemoteProfileStore>();
    }
    if (!services.cache.cache_manager) {
        services.cache.cache_manager = std::make_shared<::lofibox::cache::CacheManager>(std::filesystem::temp_directory_path() / "lofibox-null-cache");
    }
    if (!services.ui.theme) {
        services.ui.theme = std::make_shared<ui::UiTheme>(ui::defaultTheme());
    }
    return services;
}

} // namespace lofibox::app
