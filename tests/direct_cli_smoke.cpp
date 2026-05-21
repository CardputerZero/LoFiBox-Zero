// SPDX-License-Identifier: GPL-3.0-or-later

#include <cassert>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include "app/remote_profile_store.h"
#include "app/runtime_services.h"
#include "cache/cache_manager.h"
#include "cli/direct_cli.h"
#include "core/canvas.h"
#include "security/credential_policy.h"

namespace {

class MemoryRemoteProfileStore final : public lofibox::app::RemoteProfileStore {
public:
    std::vector<lofibox::app::RemoteServerProfile> loadProfiles() const override
    {
        return profiles;
    }

    bool saveProfiles(const std::vector<lofibox::app::RemoteServerProfile>& value) const override
    {
        profiles = value;
        return true;
    }

    bool saveCredentials(const lofibox::app::RemoteServerProfile& profile) const override
    {
        credentials = profile;
        for (auto& stored : profiles) {
            if (stored.id == profile.id) {
                stored.username = profile.username;
                stored.password = profile.password;
                stored.api_token = profile.api_token;
                stored.credential_ref = profile.credential_ref;
            }
        }
        return true;
    }

    bool deleteCredentials(const lofibox::security::CredentialRef& credential_ref) const override
    {
        for (auto& stored : profiles) {
            if (stored.credential_ref.id == credential_ref.id) {
                stored.password.clear();
                stored.api_token.clear();
            }
        }
        return true;
    }

    mutable std::vector<lofibox::app::RemoteServerProfile> profiles{};
    mutable lofibox::app::RemoteServerProfile credentials{};
};

class FakeRemoteSourceProvider final : public lofibox::app::RemoteSourceProvider {
public:
    bool available() const override { return true; }
    std::string displayName() const override { return "SOURCE"; }
    lofibox::app::RemoteSourceSession probe(const lofibox::app::RemoteServerProfile& profile) const override
    {
        return {!profile.base_url.empty(), profile.name, profile.username, "token", profile.base_url.empty() ? "MISSING URL" : "OK"};
    }
};

class FakeRemoteCatalogProvider final : public lofibox::app::RemoteCatalogProvider {
public:
    bool available() const override { return true; }
    std::string displayName() const override { return "CATALOG"; }
    std::vector<lofibox::app::RemoteTrack> searchTracks(const lofibox::app::RemoteServerProfile&, const lofibox::app::RemoteSourceSession&, std::string_view, int) const override { return {}; }
    std::vector<lofibox::app::RemoteTrack> recentTracks(const lofibox::app::RemoteServerProfile&, const lofibox::app::RemoteSourceSession&, int) const override { return {}; }
};

class FakeRemoteStreamResolver final : public lofibox::app::RemoteStreamResolver {
public:
    bool available() const override { return true; }
    std::string displayName() const override { return "RESOLVER"; }
    std::optional<lofibox::app::ResolvedRemoteStream> resolveTrack(const lofibox::app::RemoteServerProfile&, const lofibox::app::RemoteSourceSession&, const lofibox::app::RemoteTrack&) const override { return std::nullopt; }
};

class FakeConnectivityProvider final : public lofibox::app::ConnectivityProvider {
public:
    bool connected() const override { return true; }
    std::string displayName() const override { return "ONLINE"; }
};

class FakeMetadataProvider final : public lofibox::app::MetadataProvider {
public:
    [[nodiscard]] bool available() const override { return true; }
    [[nodiscard]] std::string displayName() const override { return "FAKE-METADATA"; }
    [[nodiscard]] lofibox::app::TrackMetadata read(const std::filesystem::path&, lofibox::app::MetadataReadMode mode) const override
    {
        lofibox::app::TrackMetadata metadata{};
        metadata.title = mode == lofibox::app::MetadataReadMode::AllowOnline ? "Looked Up Song" : "Local Song";
        metadata.artist = "CLI Artist";
        metadata.album = "CLI Album";
        metadata.duration_seconds = 123;
        return metadata;
    }
};

class FakeLyricsProvider final : public lofibox::app::LyricsProvider {
public:
    [[nodiscard]] bool available() const override { return true; }
    [[nodiscard]] std::string displayName() const override { return "FAKE-LYRICS"; }
    [[nodiscard]] lofibox::app::TrackLyrics fetch(const std::filesystem::path&, const lofibox::app::TrackMetadata&) const override
    {
        lofibox::app::TrackLyrics lyrics{};
        lyrics.plain = "hello\nworld";
        lyrics.synced = "[00:01.00]hello\n[00:02.00]world";
        lyrics.source = "LRCLIB";
        return lyrics;
    }
};

class FakeTrackIdentityProvider final : public lofibox::app::TrackIdentityProvider {
public:
    [[nodiscard]] bool available() const override { return true; }
    [[nodiscard]] std::string displayName() const override { return "FAKE-FINGERPRINT"; }
    [[nodiscard]] lofibox::app::TrackIdentity resolve(const std::filesystem::path&, const lofibox::app::TrackMetadata&) const override
    {
        lofibox::app::TrackIdentity identity{};
        identity.found = true;
        identity.source = "AcoustID";
        identity.confidence = 0.91;
        identity.fingerprint = "fp-test";
        identity.recording_mbid = "recording-test";
        identity.metadata.title = "Identified Song";
        identity.audio_fingerprint_verified = true;
        return identity;
    }
};

class FakeArtworkProvider final : public lofibox::app::ArtworkProvider {
public:
    [[nodiscard]] bool available() const override { return true; }
    [[nodiscard]] std::string displayName() const override { return "FAKE-ARTWORK"; }
    [[nodiscard]] std::optional<lofibox::core::Canvas> read(const std::filesystem::path&, lofibox::app::ArtworkReadMode) const override
    {
        return lofibox::core::Canvas{4, 3};
    }
};

std::optional<int> run(std::vector<std::string> args, lofibox::app::RuntimeServices& services, std::string& out_text, std::string& err_text)
{
    std::vector<char*> argv{};
    argv.reserve(args.size());
    for (auto& arg : args) {
        argv.push_back(arg.data());
    }
    std::ostringstream out{};
    std::ostringstream err{};
    const auto result = lofibox::cli::runDirectCliCommand(static_cast<int>(argv.size()), argv.data(), services, out, err);
    out_text = out.str();
    err_text = err.str();
    return result;
}

void touchFile(const std::filesystem::path& path)
{
    std::ofstream output(path, std::ios::binary);
    output << "test";
}

} // namespace

int main()
{
    const auto root = std::filesystem::temp_directory_path() / "lofibox_direct_cli_smoke";
    std::error_code ec{};
    std::filesystem::remove_all(root, ec);
    std::filesystem::create_directories(root / "Artist" / "Album", ec);
    touchFile(root / "Artist" / "Album" / "Song.mp3");

    auto store = std::make_shared<MemoryRemoteProfileStore>();
    auto services = lofibox::app::withNullRuntimeServices();
    services.connectivity.provider = std::make_shared<FakeConnectivityProvider>();
    services.remote.remote_profile_store = store;
    services.remote.remote_source_provider = std::make_shared<FakeRemoteSourceProvider>();
    services.remote.remote_catalog_provider = std::make_shared<FakeRemoteCatalogProvider>();
    services.remote.remote_stream_resolver = std::make_shared<FakeRemoteStreamResolver>();
    services.cache.cache_manager = std::make_shared<lofibox::cache::CacheManager>(root / "cache");
    services.cache.cache_manager->putText(lofibox::cache::CacheBucket::Metadata, "probe", "value");
    services.metadata.metadata_provider = std::make_shared<FakeMetadataProvider>();
    services.metadata.lyrics_provider = std::make_shared<FakeLyricsProvider>();
    services.metadata.track_identity_provider = std::make_shared<FakeTrackIdentityProvider>();
    services.metadata.artwork_provider = std::make_shared<FakeArtworkProvider>();

    std::string out{};
    std::string err{};
    auto code = run({"lofibox", "source", "add", "emby", "--id", "emby-home", "--name", "Emby", "--base-url", "http://remote", "--username", "vic", "--password", "secret"}, services, out, err);
    assert(code && *code == 0);
    assert(store->profiles.size() == 1U);
    assert(store->profiles.front().id == "emby-home");
    assert(store->profiles.front().password == "secret");

    code = run({"lofibox", "source", "list", "--json"}, services, out, err);
    assert(code && *code == 0);
    assert(out.find("\"id\":\"emby-home\"") != std::string::npos);

    code = run({"lofibox", "source", "show", "emby-home", "--fields", "id,readiness", "--porcelain"}, services, out, err);
    assert(code && *code == 0);
    assert(out.find("emby-home") != std::string::npos);

    code = run({"lofibox", "source", "capabilities", "emby-home", "--json"}, services, out, err);
    assert(code && *code == 0);
    assert(out.find("\"credentials\":\"supported\"") != std::string::npos);

    code = run({"lofibox", "source", "probe", "emby-home"}, services, out, err);
    assert(code && *code == 0);
    assert(out.find("ONLINE") != std::string::npos);

    code = run({"lofibox", "credentials", "list", "--json"}, services, out, err);
    assert(code && *code == 0);
    assert(out.find("\"credential_ref\":\"credential/emby-home\"") != std::string::npos);

    code = run({"lofibox", "credentials", "show-ref", "credential/emby-home", "--json"}, services, out, err);
    assert(code && *code == 0);
    assert(out.find("\"password\":\"SET\"") != std::string::npos);

    code = run({"lofibox", "credentials", "delete", "emby-home"}, services, out, err);
    assert(code && *code == 0);
    assert(store->profiles.front().password.empty());

    code = run({"lofibox", "source", "local-root", "add", root.string(), "--name", "Local", "--json"}, services, out, err);
    assert(code && *code == 0);
    assert(out.find("\"kind\":\"local-root\"") != std::string::npos);
    std::string local_root_id{};
    bool found_local_root = false;
    for (const auto& profile : store->profiles) {
        if (profile.kind == lofibox::app::RemoteServerKind::LocalRoot) {
            found_local_root = true;
            local_root_id = profile.id;
            assert(profile.enabled);
            assert(profile.local_root == root.string());
            assert(profile.credential_ref.id.empty());
        }
    }
    assert(found_local_root);
    assert(!local_root_id.empty());

    code = run({"lofibox", "source", "local-root", "list", "--json"}, services, out, err);
    assert(code && *code == 0);
    assert(out.find("\"id\":\"" + local_root_id + "\"") != std::string::npos);

    code = run({"lofibox", "library", "status", "--json"}, services, out, err);
    assert(code && *code == 0);
    assert(out.find("\"tracks\":\"1\"") != std::string::npos);

    code = run({"lofibox", "library", "root", "disable", local_root_id, "--json"}, services, out, err);
    assert(code && *code == 0);
    for (const auto& profile : store->profiles) {
        if (profile.id == local_root_id) {
            assert(!profile.enabled);
        }
    }

    code = run({"lofibox", "source", "local-root", "enable", root.string(), "--json"}, services, out, err);
    assert(code && *code == 0);
    for (const auto& profile : store->profiles) {
        if (profile.id == local_root_id) {
            assert(profile.enabled);
        }
    }

    code = run({"lofibox", "library", "root", "remove", local_root_id, "--json"}, services, out, err);
    assert(code && *code == 0);
    for (const auto& profile : store->profiles) {
        assert(profile.kind != lofibox::app::RemoteServerKind::LocalRoot);
    }

    code = run({"lofibox", "library", "scan", root.string(), "--json"}, services, out, err);
    assert(code && *code == 0);
    assert(out.find("\"tracks\":\"1\"") != std::string::npos);

    code = run({"lofibox", "library", "list", "tracks", "--root", root.string(), "--fields", "title", "--porcelain"}, services, out, err);
    assert(code && *code == 0);
    assert(out.find("Song") != std::string::npos);

    code = run({"lofibox", "library", "query", "tracks", "--artist", "CLI Artist", "--root", root.string(), "--json"}, services, out, err);
    assert(code && *code == 0);
    assert(out.find("\"artist\":\"CLI Artist\"") != std::string::npos);

    code = run({"lofibox", "library", "query", "Song", "--root", root.string()}, services, out, err);
    assert(code && *code == 0);
    assert(out.find("Song") != std::string::npos);

    code = run({"lofibox", "search", "Song", "--root", root.string(), "--json"}, services, out, err);
    assert(code && *code == 0);
    assert(out.find("\"group\":\"local\"") != std::string::npos);

    code = run({"lofibox", "remote", "browse", "emby-home", "--porcelain"}, services, out, err);
    assert(code && *code == 0);
    assert(out.find("ARTISTS") != std::string::npos);

    const auto song_path = root / "Artist" / "Album" / "Song.mp3";
    code = run({"lofibox", "metadata", "show", song_path.string(), "--json"}, services, out, err);
    assert(code && *code == 0);
    assert(out.find("\"title\":\"Local Song\"") != std::string::npos);

    code = run({"lofibox", "metadata", "lookup", song_path.string(), "--json"}, services, out, err);
    assert(code && *code == 0);
    assert(out.find("\"status\":\"LOOKED_UP\"") != std::string::npos);
    assert(out.find("\"title\":\"Looked Up Song\"") != std::string::npos);

    code = run({"lofibox", "lyrics", "lookup", song_path.string(), "--json"}, services, out, err);
    assert(code && *code == 0);
    assert(out.find("\"source\":\"LRCLIB\"") != std::string::npos);
    assert(out.find("\"plain_available\":\"true\"") != std::string::npos);

    code = run({"lofibox", "fingerprint", "generate", song_path.string(), "--json"}, services, out, err);
    assert(code && *code == 0);
    assert(out.find("\"fingerprint\":\"fp-test\"") != std::string::npos);

    code = run({"lofibox", "artwork", "lookup", song_path.string(), "--json"}, services, out, err);
    assert(code && *code == 0);
    assert(out.find("\"width\":\"4\"") != std::string::npos);

    code = run({"lofibox", "cache", "status", "--json"}, services, out, err);
    assert(code && *code == 0);
    assert(out.find("\"entries\"") != std::string::npos);

    code = run({"lofibox", "cache", "purge", "--json"}, services, out, err);
    assert(code && *code == 0);
    assert(out.find("\"status\":\"CLEARED\"") != std::string::npos);

    code = run({"lofibox", "doctor", "--json"}, services, out, err);
    assert(code.has_value());
    assert(out.find("\"capabilities\"") != std::string::npos);

    code = run({"lofibox", "play"}, services, out, err);
    assert(!code.has_value());

    std::filesystem::remove_all(root, ec);
    return 0;
}
