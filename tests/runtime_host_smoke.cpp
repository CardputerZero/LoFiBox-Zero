// SPDX-License-Identifier: GPL-3.0-or-later

#include <filesystem>
#include <iostream>
#include <memory>
#include <atomic>
#include <chrono>
#include <fstream>
#include <thread>
#include <vector>

#include "app/runtime_services.h"
#include "application/app_service_host.h"
#include "runtime/runtime_host.h"

namespace {

class RuntimeHostAudioBackend final : public lofibox::app::AudioPlaybackBackend {
public:
    struct EntryGuard {
        explicit EntryGuard(RuntimeHostAudioBackend& owner_in)
            : owner(owner_in)
        {
            if (owner.active_backend_entries.fetch_add(1) > 0) {
                owner.overlapped_backend_entries = true;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds{2});
        }

        ~EntryGuard()
        {
            owner.active_backend_entries.fetch_sub(1);
        }

        RuntimeHostAudioBackend& owner;
    };

    [[nodiscard]] bool available() const override { return true; }
    [[nodiscard]] std::string displayName() const override { return "RUNTIME-HOST-TEST"; }
    bool playFile(const std::filesystem::path& path, double) override
    {
        EntryGuard guard{*this};
        played_path = path;
        playing = true;
        return true;
    }
    bool playUri(const std::string& uri, double) override
    {
        EntryGuard guard{*this};
        played_uri = uri;
        playing = true;
        return true;
    }
    void stop() override { playing = false; }
    [[nodiscard]] bool isPlaying() override { return playing; }
    [[nodiscard]] bool isFinished() override { return false; }
    [[nodiscard]] lofibox::app::AudioPlaybackState state() override
    {
        EntryGuard guard{*this};
        return playing ? lofibox::app::AudioPlaybackState::Playing : lofibox::app::AudioPlaybackState::Idle;
    }

    std::filesystem::path played_path{};
    std::string played_uri{};
    bool playing{false};
    std::atomic<int> active_backend_entries{0};
    std::atomic_bool overlapped_backend_entries{false};
};

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

    bool deleteCredentials(const lofibox::security::CredentialRef&) const override
    {
        return true;
    }

    mutable std::vector<lofibox::app::RemoteServerProfile> profiles{};
};

class RuntimeHostMetadataProvider final : public lofibox::app::MetadataProvider {
public:
    [[nodiscard]] bool available() const override { return true; }
    [[nodiscard]] std::string displayName() const override { return "RUNTIME-HOST-METADATA"; }
    [[nodiscard]] lofibox::app::TrackMetadata read(const std::filesystem::path&, lofibox::app::MetadataReadMode = lofibox::app::MetadataReadMode::AllowOnline) const override
    {
        lofibox::app::TrackMetadata metadata{};
        metadata.title = "Runtime Refresh";
        metadata.artist = "Runtime Artist";
        metadata.album = "Runtime Album";
        return metadata;
    }
};

} // namespace

int main()
{
    const auto root = std::filesystem::temp_directory_path() / "lofibox_runtime_host_smoke";
    std::error_code ec{};
    std::filesystem::remove_all(root, ec);
    std::filesystem::create_directories(root / "Music", ec);
    {
        std::ofstream file{root / "Music" / "refresh.mp3", std::ios::binary};
        file << "test";
    }

    auto services = lofibox::app::withNullRuntimeServices();
    auto backend = std::make_shared<RuntimeHostAudioBackend>();
    services.playback.audio_backend = backend;
    services.metadata.metadata_provider = std::make_shared<RuntimeHostMetadataProvider>();
    auto store = std::make_shared<MemoryRemoteProfileStore>();
    lofibox::app::RemoteServerProfile local_root{};
    local_root.kind = lofibox::app::RemoteServerKind::LocalRoot;
    local_root.id = "local-root-runtime";
    local_root.name = "Runtime Music";
    local_root.local_root = (root / "Music").string();
    local_root.base_url = local_root.local_root;
    local_root.enabled = true;
    local_root.default_eligible = true;
    store->profiles.push_back(local_root);
    services.remote.remote_profile_store = store;

    lofibox::application::AppServiceHost app_host{services};
    app_host.controllers().library.mutableModel().tracks.push_back(
        lofibox::app::TrackRecord{41, std::filesystem::path{"host.mp3"}, "Host", "Artist", "Album"});
    app_host.controllers().library.setSongsContextAll();

    lofibox::runtime::RuntimeHost runtime_host{app_host.registry()};
    const auto started = runtime_host.client().dispatch(lofibox::runtime::RuntimeCommand{
        lofibox::runtime::RuntimeCommandKind::PlaybackStartTrack,
        lofibox::runtime::RuntimeCommandPayload::startTrack(41),
        lofibox::runtime::CommandOrigin::DirectTest,
        "host-start"});
    if (!started.accepted || !started.applied || backend->played_path != "host.mp3") {
        std::cerr << "Expected RuntimeHost to own live playback dispatch outside AppRuntimeContext.\n";
        return 1;
    }

    runtime_host.tick(0.25);
    const auto snapshot = runtime_host.client().query(lofibox::runtime::RuntimeQuery{
        lofibox::runtime::RuntimeQueryKind::FullSnapshot,
        lofibox::runtime::CommandOrigin::DirectTest,
        "host-snapshot"});
    if (snapshot.playback.current_track_id != 41 || snapshot.version != 1U) {
        std::cerr << "Expected RuntimeHost client snapshot to expose the hosted runtime state.\n";
        return 1;
    }

    std::thread ticker{[&runtime_host]() {
        for (int index = 0; index < 80; ++index) {
            runtime_host.tick(0.01);
        }
    }};
    std::thread dispatcher{[&runtime_host]() {
        for (int index = 0; index < 20; ++index) {
            (void)runtime_host.client().dispatch(lofibox::runtime::RuntimeCommand{
                lofibox::runtime::RuntimeCommandKind::PlaybackStartTrack,
                lofibox::runtime::RuntimeCommandPayload::startTrack(41),
                lofibox::runtime::CommandOrigin::DirectTest});
        }
    }};
    ticker.join();
    dispatcher.join();
    if (backend->overlapped_backend_entries) {
        std::cerr << "Expected RuntimeHost tick and external commands to be serialized through the runtime bus.\n";
        return 1;
    }

    const auto refresh = runtime_host.client().dispatch(lofibox::runtime::RuntimeCommand{
        lofibox::runtime::RuntimeCommandKind::LibraryRefresh,
        {},
        lofibox::runtime::CommandOrigin::DirectTest,
        "library-refresh"});
    if (!refresh.accepted || !refresh.applied || refresh.code != "LIBRARY_REFRESH") {
        std::cerr << "Expected runtime LibraryRefresh to request configured-root indexing.\n";
        return 1;
    }
    bool refreshed = false;
    for (int attempt = 0; attempt < 100; ++attempt) {
        if (app_host.registry().libraryMutations().pollAsyncRefreshLibrary()) {
            refreshed = true;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds{5});
    }
    if (!refreshed || app_host.registry().libraryQueries().model().tracks.size() != 1U) {
        std::cerr << "Expected runtime LibraryRefresh to rebuild from SourceProfilesDomain local roots.\n";
        return 1;
    }

    std::filesystem::remove_all(root, ec);
    return 0;
}
