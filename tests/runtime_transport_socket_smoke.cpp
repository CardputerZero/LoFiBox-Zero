// SPDX-License-Identifier: GPL-3.0-or-later

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "app/remote_profile_store.h"
#include "app/runtime_services.h"
#include "application/app_service_host.h"
#include "cli/direct_cli.h"
#include "runtime/runtime_host.h"
#include "runtime/unix_socket_runtime_transport.h"

namespace {

class RuntimeSocketAudioBackend final : public lofibox::app::AudioPlaybackBackend {
public:
    [[nodiscard]] bool available() const override { return true; }
    [[nodiscard]] std::string displayName() const override { return "RUNTIME-SOCKET-TEST"; }
    bool playFile(const std::filesystem::path&, double) override
    {
        playing = true;
        return true;
    }
    void stop() override { playing = false; }
    [[nodiscard]] bool isPlaying() override { return playing; }
    [[nodiscard]] bool isFinished() override { return false; }

    bool playing{false};
};

class RuntimeSocketMetadataProvider final : public lofibox::app::MetadataProvider {
public:
    [[nodiscard]] bool available() const override { return true; }
    [[nodiscard]] std::string displayName() const override { return "RUNTIME-SOCKET-METADATA"; }
    [[nodiscard]] lofibox::app::TrackMetadata read(const std::filesystem::path&, lofibox::app::MetadataReadMode = lofibox::app::MetadataReadMode::AllowOnline) const override
    {
        lofibox::app::TrackMetadata metadata{};
        metadata.title = "Socket Refresh";
        metadata.artist = "Socket Artist";
        metadata.album = "Socket Album";
        return metadata;
    }
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

std::optional<int> runDirect(std::vector<std::string> args, lofibox::app::RuntimeServices& services, std::string& out_text, std::string& err_text)
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
    std::ofstream output{path, std::ios::binary};
    output << "test";
}

} // namespace

int main()
{
#if defined(__unix__) || defined(__APPLE__)
    const auto root = std::filesystem::temp_directory_path() / "lofibox_runtime_transport_socket_smoke";
    const auto runtime_dir = root / "runtime";
    const auto media_root = root / "Music";
    std::error_code ec{};
    std::filesystem::remove_all(root, ec);
    std::filesystem::create_directories(runtime_dir, ec);
    std::filesystem::create_directories(media_root, ec);
    touchFile(media_root / "socket-refresh.mp3");
    setenv("XDG_RUNTIME_DIR", runtime_dir.string().c_str(), 1);

    auto services = lofibox::app::withNullRuntimeServices();
    services.playback.audio_backend = std::make_shared<RuntimeSocketAudioBackend>();
    services.metadata.metadata_provider = std::make_shared<RuntimeSocketMetadataProvider>();
    services.remote.remote_profile_store = std::make_shared<MemoryRemoteProfileStore>();

    lofibox::application::AppServiceHost app_host{services};
    app_host.controllers().library.mutableModel().tracks.push_back(
        lofibox::app::TrackRecord{51, std::filesystem::path{"socket.mp3"}, "Socket", "Artist", "Album"});
    app_host.controllers().library.setSongsContextAll();

    const auto socket_path = std::filesystem::temp_directory_path() / "lofibox-runtime-transport-smoke.sock";
    std::filesystem::remove(socket_path, ec);

    lofibox::runtime::RuntimeHost runtime_host{app_host.registry()};
    if (!runtime_host.startExternalTransport(socket_path)) {
        std::cerr << "Expected RuntimeHost to start external socket transport: "
                  << runtime_host.externalTransportError() << '\n';
        return 1;
    }

    lofibox::runtime::UnixSocketRuntimeCommandClient client{socket_path};
    const auto result = client.dispatch(lofibox::runtime::RuntimeCommand{
        lofibox::runtime::RuntimeCommandKind::PlaybackStartTrack,
        lofibox::runtime::RuntimeCommandPayload::startTrack(51),
        lofibox::runtime::CommandOrigin::RuntimeCli,
        "socket-start"});
    if (!result.accepted || !result.applied || result.origin != lofibox::runtime::CommandOrigin::RuntimeCli) {
        std::cerr << "Expected socket runtime client to apply a playback command through transport.\n";
        return 1;
    }

    const auto snapshot = client.query(lofibox::runtime::RuntimeQuery{
        lofibox::runtime::RuntimeQueryKind::PlaybackSnapshot,
        lofibox::runtime::CommandOrigin::RuntimeCli,
        "socket-playback"});
    if (snapshot.playback.current_track_id != 51 || snapshot.playback.title != "Socket") {
        std::cerr << "Expected socket runtime query to return the hosted playback snapshot.\n";
        return 1;
    }

    app_host.controllers().library.mutableModel().tracks.clear();
    app_host.controllers().library.setSongsContextAll();
    std::string out{};
    std::string err{};
    const auto add_result = runDirect(
        {"lofibox", "source", "local-root", "add", media_root.string(), "--name", "Socket Music", "--runtime-socket", socket_path.string()},
        services,
        out,
        err);
    if (!add_result || *add_result != 0) {
        std::cerr << "Expected direct local-root add to succeed: " << err << '\n';
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
        std::cerr << "Expected direct local-root mutation to notify the running instance to refresh configured roots.\n";
        return 1;
    }

    runtime_host.stopExternalTransport();
    std::filesystem::remove(socket_path, ec);
    std::filesystem::remove_all(root, ec);
#endif
    return 0;
}
