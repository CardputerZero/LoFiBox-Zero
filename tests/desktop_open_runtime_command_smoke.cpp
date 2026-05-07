// SPDX-License-Identifier: GPL-3.0-or-later

#include <filesystem>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <utility>

#include "app/app_runtime_context.h"
#include "application/app_service_host.h"
#include "runtime/runtime_host.h"

namespace {

class DesktopOpenAudioBackend final : public lofibox::app::AudioPlaybackBackend {
public:
    [[nodiscard]] bool available() const override { return true; }
    [[nodiscard]] std::string displayName() const override { return "DESKTOP-OPEN-TEST"; }
    bool playFile(const std::filesystem::path&, double) override { return false; }
    bool playUri(const std::string& uri, double) override
    {
        played_uri = uri;
        playing = true;
        return true;
    }
    void stop() override { playing = false; }
    [[nodiscard]] bool isPlaying() override { return playing; }
    [[nodiscard]] bool isFinished() override { return false; }

    std::string played_uri{};
    bool playing{false};
};

class DesktopOpenRemoteSourceProvider final : public lofibox::app::RemoteSourceProvider {
public:
    [[nodiscard]] bool available() const override { return true; }
    [[nodiscard]] std::string displayName() const override { return "DESKTOP-OPEN-SOURCE"; }
    [[nodiscard]] lofibox::app::RemoteSourceSession probe(const lofibox::app::RemoteServerProfile& profile) const override
    {
        lofibox::app::RemoteSourceSession session{};
        session.available = !profile.base_url.empty();
        session.server_name = "Desktop Open";
        session.message = session.available ? "ONLINE" : "MISSING URL";
        return session;
    }
};

class DesktopOpenStreamResolver final : public lofibox::app::RemoteStreamResolver {
public:
    [[nodiscard]] bool available() const override { return true; }
    [[nodiscard]] std::string displayName() const override { return "DESKTOP-OPEN-RESOLVER"; }
    [[nodiscard]] std::optional<lofibox::app::ResolvedRemoteStream> resolveTrack(
        const lofibox::app::RemoteServerProfile& profile,
        const lofibox::app::RemoteSourceSession& session,
        const lofibox::app::RemoteTrack& track) const override
    {
        if (!session.available || profile.base_url.empty()) {
            return std::nullopt;
        }
        lofibox::app::ResolvedRemoteStream stream{};
        stream.url = track.id.empty() ? profile.base_url : track.id;
        stream.diagnostics.source_name = profile.name;
        stream.diagnostics.resolved_url_redacted = profile.base_url;
        stream.diagnostics.connected = true;
        stream.diagnostics.connection_status = "READY";
        return stream;
    }
};

} // namespace

int main()
{
    auto backend = std::make_shared<DesktopOpenAudioBackend>();
    auto services = lofibox::app::withNullRuntimeServices();
    services.playback.audio_backend = backend;
    services.remote.remote_source_provider = std::make_shared<DesktopOpenRemoteSourceProvider>();
    services.remote.remote_stream_resolver = std::make_shared<DesktopOpenStreamResolver>();

    lofibox::application::AppServiceHost app_host{services};
    lofibox::runtime::RuntimeHost runtime_host{app_host.registry()};
    lofibox::app::AppRuntimeContext app{
        {},
        {},
        {},
        app_host,
        runtime_host.client(),
        {"https://example.test/desktop-open.mp3"}};
    app.update();

    if (backend->played_uri != "https://example.test/desktop-open.mp3") {
        std::cerr << "Expected desktop-open URL playback to reach the audio backend through runtime command handling.\n";
        return 1;
    }
    if (app.currentPage() != lofibox::app::AppPage::NowPlaying) {
        std::cerr << "Expected desktop-open URL playback to navigate to Now Playing.\n";
        return 1;
    }
    if (app.playbackSession().current_stream_source != "DESKTOP OPEN") {
        std::cerr << "Expected transient desktop-open source label to survive runtime command dispatch.\n";
        return 1;
    }

    return 0;
}
