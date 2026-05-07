// SPDX-License-Identifier: GPL-3.0-or-later

#include <iostream>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "app/app_runtime_context.h"
#include "runtime/runtime_host.h"
#include "app/runtime_services.h"
#include "application/app_service_host.h"
#include "cache/cache_manager.h"
#include "remote/common/remote_source_registry.h"

namespace {

class FakeRemoteProfileStore final : public lofibox::app::RemoteProfileStore {
public:
    std::vector<lofibox::app::RemoteServerProfile> loadProfiles() const override
    {
        return {lofibox::app::RemoteServerProfile{
            lofibox::app::RemoteServerKind::DirectUrl,
            "direct",
            "Test Stream",
            "https://example.test/audio.mp3"}};
    }

    bool saveProfiles(const std::vector<lofibox::app::RemoteServerProfile>&) const override { return true; }
};

class FakeRemoteSourceProvider final : public lofibox::app::RemoteSourceProvider {
public:
    bool available() const override { return true; }
    std::string displayName() const override { return "REMOTE"; }
    lofibox::app::RemoteSourceSession probe(const lofibox::app::RemoteServerProfile& profile) const override
    {
        return {true, profile.name, "", "", "OK"};
    }
};

class FakeRemoteCatalogProvider final : public lofibox::app::RemoteCatalogProvider {
public:
    bool available() const override { return true; }
    std::string displayName() const override { return "CATALOG"; }

    std::vector<lofibox::app::RemoteTrack> searchTracks(
        const lofibox::app::RemoteServerProfile&,
        const lofibox::app::RemoteSourceSession&,
        std::string_view,
        int) const override
    {
        return {{"https://example.test/search.mp3", "Search Hit", "Remote", "", "", 0}};
    }

    std::vector<lofibox::app::RemoteTrack> recentTracks(
        const lofibox::app::RemoteServerProfile&,
        const lofibox::app::RemoteSourceSession&,
        int) const override
    {
        return {};
    }

    std::vector<lofibox::app::RemoteTrack> libraryTracks(
        const lofibox::app::RemoteServerProfile& profile,
        const lofibox::app::RemoteSourceSession&,
        int) const override
    {
        return {{profile.base_url, profile.base_url, "", "", "", 0}};
    }

    std::vector<lofibox::app::RemoteCatalogNode> browse(
        const lofibox::app::RemoteServerProfile& profile,
        const lofibox::app::RemoteSourceSession&,
        const lofibox::app::RemoteCatalogNode&,
        int) const override
    {
        return {{lofibox::app::RemoteCatalogNodeKind::Tracks, profile.base_url, "UNKNOWN", "UNKNOWN", true, false}};
    }
};

class FakeRemoteStreamResolver final : public lofibox::app::RemoteStreamResolver {
public:
    bool available() const override { return true; }
    std::string displayName() const override { return "RESOLVER"; }
    std::optional<lofibox::app::ResolvedRemoteStream> resolveTrack(
        const lofibox::app::RemoteServerProfile& profile,
        const lofibox::app::RemoteSourceSession&,
        const lofibox::app::RemoteTrack& track) const override
    {
        lofibox::app::ResolvedRemoteStream stream{};
        stream.url = track.id.empty() ? profile.base_url : track.id;
        stream.seekable = true;
        stream.diagnostics.source_name = profile.name;
        stream.diagnostics.resolved_url_redacted = "https://example.test/[redacted]";
        stream.diagnostics.connected = true;
        stream.diagnostics.connection_status = "READY";
        return stream;
    }
};

class FakeAudioBackend final : public lofibox::app::AudioPlaybackBackend {
public:
    bool available() const override { return true; }
    std::string displayName() const override { return "AUDIO"; }
    bool playFile(const std::filesystem::path&, double) override { return false; }
    bool playUri(const std::string& uri, double) override
    {
        played_uri = uri;
        finished = false;
        paused = false;
        return true;
    }
    void stop() override { finished = true; }
    void pause() override { paused = true; }
    void resume() override { paused = false; }
    bool isPlaying() override { return !played_uri.empty() && !finished && !paused; }
    bool isFinished() override { return finished; }
    lofibox::app::AudioVisualizationFrame visualizationFrame() const override
    {
        lofibox::app::AudioVisualizationFrame frame{};
        if (!played_uri.empty() && !finished) {
            frame.available = true;
            frame.bands[0] = 0.25f;
            frame.bands[4] = 0.75f;
            frame.bands[9] = 0.45f;
        }
        return frame;
    }

    std::string played_uri{};
    bool finished{false};
    bool paused{false};
};

class AlwaysOnline final : public lofibox::app::ConnectivityProvider {
public:
    bool connected() const override { return true; }
    std::string displayName() const override { return "ONLINE"; }
};

} // namespace

int main()
{
    auto audio = std::make_shared<FakeAudioBackend>();
    lofibox::app::RuntimeServices services = lofibox::app::withNullRuntimeServices({});
    services.connectivity.provider = std::make_shared<AlwaysOnline>();
    services.remote.remote_profile_store = std::make_shared<FakeRemoteProfileStore>();
    services.remote.remote_source_provider = std::make_shared<FakeRemoteSourceProvider>();
    services.remote.remote_catalog_provider = std::make_shared<FakeRemoteCatalogProvider>();
    services.remote.remote_stream_resolver = std::make_shared<FakeRemoteStreamResolver>();
    services.playback.audio_backend = audio;
    const auto cache_root = std::filesystem::temp_directory_path() / "lofibox-remote-readonly-cache-smoke";
    const auto media_root = std::filesystem::temp_directory_path() / "lofibox-remote-unified-library-media";
    std::error_code ec{};
    std::filesystem::remove_all(cache_root, ec);
    std::filesystem::remove_all(media_root, ec);
    std::filesystem::create_directories(media_root, ec);
    services.cache.cache_manager = std::make_shared<lofibox::cache::CacheManager>(cache_root);
    services.cache.cache_manager->putText(
        lofibox::cache::CacheBucket::Metadata,
        "remote-media-direct-url-direct-https://example.test/audio.mp3",
        "version=1\nid=https://example.test/audio.mp3\ntitle=Cached Stream Title\nartist=Cached Artist\nalbum=Cached Album\nalbum_id=\nduration=123\n");

    lofibox::application::AppServiceHost app_host{services};
    lofibox::runtime::RuntimeHost runtime_host{app_host.registry()};
    lofibox::app::AppRuntimeContext app{{media_root}, {}, {}, app_host, runtime_host.client()};
    app.update();
    for (int tick = 0; tick < 500; ++tick) {
        app.update();
        if (app.snapshot().library_ready) {
            break;
        }
    }
    app.pushPage(lofibox::app::AppPage::MusicIndex);
    const auto library_rows = app.pageModel().rows;
    if (library_rows.size() != 7U) {
        std::cerr << "Expected MusicIndex to keep unified Library rows plus remote source shortcuts.\n";
        std::filesystem::remove_all(cache_root, ec);
        std::filesystem::remove_all(media_root, ec);
        return 1;
    }
    if (library_rows[1].second != "1" || library_rows[2].second != "1") {
        std::cerr << "Expected unified Library counts to include remote albums and songs.\n";
        std::filesystem::remove_all(cache_root, ec);
        std::filesystem::remove_all(media_root, ec);
        return 1;
    }
    if (library_rows.back().first != "Test Stream" || library_rows.back().second != "1") {
        std::cerr << "Expected remote source shortcut to show its unified Library song count.\n";
        std::filesystem::remove_all(cache_root, ec);
        std::filesystem::remove_all(media_root, ec);
        return 1;
    }

    app.navigationState().list_selection.selected = 1;
    app.confirmListPage();
    if (app.currentPage() != lofibox::app::AppPage::Albums || app.pageModel().rows.empty() || app.pageModel().rows.front().first != "Cached Album") {
        std::cerr << "Expected Albums to expose remote albums through the unified Library model.\n";
        std::filesystem::remove_all(cache_root, ec);
        std::filesystem::remove_all(media_root, ec);
        return 1;
    }

    app.navigationState().list_selection.selected = 0;
    app.confirmListPage();
    if (app.currentPage() != lofibox::app::AppPage::Songs || app.pageModel().rows.empty() || app.pageModel().rows.front().first != "Cached Stream Title") {
        std::cerr << "Expected remote album open to expose remote tracks in Songs.\n";
        std::filesystem::remove_all(cache_root, ec);
        std::filesystem::remove_all(media_root, ec);
        return 1;
    }

    app.navigationState().list_selection.selected = 0;
    app.confirmListPage();
    if (app.currentPage() != lofibox::app::AppPage::NowPlaying || audio->played_uri != "https://example.test/audio.mp3") {
        std::cerr << "Expected unified Library remote song to start resolved remote playback.\n";
        std::filesystem::remove_all(cache_root, ec);
        std::filesystem::remove_all(media_root, ec);
        return 1;
    }
    if (!app.playbackSession().current_track_id) {
        std::cerr << "Expected remote Library playback to keep a unified current track id.\n";
        std::filesystem::remove_all(cache_root, ec);
        std::filesystem::remove_all(media_root, ec);
        return 1;
    }
    const auto* now_playing_track = app.findTrack(*app.playbackSession().current_track_id);
    if (now_playing_track == nullptr || !now_playing_track->remote || now_playing_track->source_label != "Test Stream") {
        std::cerr << "Expected Now Playing track to retain remote source provenance.\n";
        std::filesystem::remove_all(cache_root, ec);
        std::filesystem::remove_all(media_root, ec);
        return 1;
    }
    app.pausePlayback();
    if (!audio->paused || app.playbackSession().status == lofibox::app::PlaybackStatus::Playing) {
        std::cerr << "Expected remote Library playback to pause through the normal playback controls.\n";
        std::filesystem::remove_all(cache_root, ec);
        std::filesystem::remove_all(media_root, ec);
        return 1;
    }
    app.playFromMenu();
    if (audio->paused || app.playbackSession().status != lofibox::app::PlaybackStatus::Playing) {
        std::cerr << "Expected remote Library playback to resume from main menu controls.\n";
        std::filesystem::remove_all(cache_root, ec);
        std::filesystem::remove_all(media_root, ec);
        return 1;
    }
    runtime_host.tick(0.2);
    if (!app.playbackSession().visualization_frame.available) {
        std::cerr << "Expected remote playback to keep Now Playing visualization available.\n";
        std::filesystem::remove_all(cache_root, ec);
        std::filesystem::remove_all(media_root, ec);
        return 1;
    }
    runtime_host.tick(200.0);
    audio->finished = true;
    runtime_host.tick(0.5);
    if (app.playbackSession().status == lofibox::app::PlaybackStatus::Playing || app.playbackSession().elapsed_seconds > 123.01) {
        std::cerr << "Expected finished remote track to stop advancing elapsed time at track duration.\n";
        std::filesystem::remove_all(cache_root, ec);
        std::filesystem::remove_all(media_root, ec);
        return 1;
    }

    audio->played_uri.clear();
    audio->finished = false;
    audio->paused = false;
    app.pushPage(lofibox::app::AppPage::SourceManager);
    auto source_rows = app.pageModel().rows;
    const int selected_profile_row = static_cast<int>(source_rows.size()) - 1;
    app.navigationState().list_selection.selected = selected_profile_row;
    app.confirmListPage();
    if (app.currentPage() != lofibox::app::AppPage::RemoteBrowse) {
        std::cerr << "Expected profile selection to open RemoteBrowse.\n";
        return 1;
    }

    if (app.pageModel().rows.empty() || app.pageModel().rows.front().first != "UNKNOWN") {
        std::cerr << "Expected RemoteBrowse to show playable catalog node.\n";
        return 1;
    }

    app.navigationState().list_selection.selected = 0;
    app.confirmListPage();
    if (app.currentPage() != lofibox::app::AppPage::NowPlaying) {
        std::cerr << "Expected playable remote node to start playback without showing StreamDetail.\n";
        return 1;
    }
    if (audio->played_uri != "https://example.test/audio.mp3") {
        std::cerr << "Expected audio backend to receive resolved remote URL.\n";
        return 1;
    }
    if (!app.playbackSession().current_track_id) {
        std::cerr << "Expected source shortcut playback to keep a unified Library track id.\n";
        std::filesystem::remove_all(cache_root, ec);
        return 1;
    }
    const auto* shortcut_track = app.findTrack(*app.playbackSession().current_track_id);
    if (shortcut_track == nullptr || shortcut_track->title != "Cached Stream Title") {
        std::cerr << "Expected playback session to expose cached read-only remote track title through Library.\n";
        std::filesystem::remove_all(cache_root, ec);
        return 1;
    }

    std::filesystem::remove_all(cache_root, ec);
    std::filesystem::remove_all(media_root, ec);
    return 0;
}
