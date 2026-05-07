// SPDX-License-Identifier: GPL-3.0-or-later

#include <chrono>
#include <filesystem>
#include <iostream>
#include <memory>
#include <optional>
#include <thread>

#include "app/input_event.h"
#include "app/lofibox_app.h"
#include "app/runtime_services.h"
#include "core/canvas.h"
#include "core/color.h"
#include "core/display_profile.h"
#include "platform/host/runtime_provider_factories.h"
#include "media_fixture_utils.h"

namespace fs = std::filesystem;

namespace {

class FixtureMetadataProvider final : public lofibox::app::MetadataProvider {
public:
    [[nodiscard]] bool available() const override { return true; }
    [[nodiscard]] std::string displayName() const override { return "FIXTURE"; }
    [[nodiscard]] lofibox::app::TrackMetadata read(const std::filesystem::path&, lofibox::app::MetadataReadMode) const override
    {
        return lofibox::app::TrackMetadata{
            std::string{"Artwork Song"},
            std::string{"Artwork Artist"},
            std::string{"Artwork Album"},
            std::nullopt,
            std::nullopt,
            1};
    }
};

class OfflineConnectivityProvider final : public lofibox::app::ConnectivityProvider {
public:
    [[nodiscard]] bool connected() const override { return false; }
    [[nodiscard]] std::string displayName() const override { return "OFFLINE"; }
};

class NoopTagWriter final : public lofibox::app::TagWriter {
public:
    [[nodiscard]] bool available() const override { return false; }
    [[nodiscard]] std::string displayName() const override { return "NOOP"; }
    bool write(const std::filesystem::path&, const lofibox::app::TagWriteRequest&) const override { return false; }
};

class FakeAudioBackend final : public lofibox::app::AudioPlaybackBackend {
public:
    [[nodiscard]] bool available() const override { return true; }
    [[nodiscard]] std::string displayName() const override { return "FAKE"; }
    bool playFile(const std::filesystem::path&, double) override
    {
        playing = true;
        paused = false;
        return true;
    }
    void stop() override { playing = false; }
    void pause() override { paused = true; }
    void resume() override { playing = true; paused = false; }
    [[nodiscard]] bool isPlaying() override { return playing && !paused; }
    [[nodiscard]] bool isFinished() override { return false; }

    bool playing{false};
    bool paused{false};
};

int countRedCoverPixels(const lofibox::core::Canvas& canvas)
{
    int red_dominant_pixels = 0;
    for (int y = 44; y < 120; ++y) {
        for (int x = 30; x < 108; ++x) {
            const auto pixel = canvas.pixel(x, y);
            if (pixel.r > 180 && pixel.g < 100 && pixel.b < 100) {
                ++red_dominant_pixels;
            }
        }
    }
    return red_dominant_pixels;
}

} // namespace

int main()
{
    #if defined(_WIN32)
    const auto ffmpeg_path = test_media_fixture::resolveExecutablePath(L"FFMPEG_PATH", L"ffmpeg.exe");
    #elif defined(__linux__)
    const auto ffmpeg_path = test_media_fixture::resolveExecutablePath("FFMPEG_PATH", "ffmpeg");
    #else
    const auto ffmpeg_path = std::optional<fs::path>{};
    #endif
    if (!ffmpeg_path.has_value()) {
        std::cout << "ffmpeg not found; skipping now playing artwork smoke.\n";
        return 0;
    }

    const auto run_id = std::chrono::steady_clock::now().time_since_epoch().count();
    const fs::path root = fs::temp_directory_path() / ("lofibox_zero_now_playing_artwork_smoke_" + std::to_string(run_id));
    std::error_code ec{};
    fs::remove_all(root, ec);
    fs::create_directories(root / "Artist" / "Album", ec);
    if (ec) {
        std::cerr << "Failed to create temp directory.\n";
        return 1;
    }

    const fs::path cover_path = root / "cover.png";
    const std::vector<std::string> cover_args{
        "-loglevel", "error",
        "-nostdin",
        "-f", "lavfi",
        "-i", "color=c=red:s=32x32:d=1",
        "-frames:v", "1",
        "-y",
        cover_path.string()};
    if (!test_media_fixture::runCommand(*ffmpeg_path, cover_args) || !fs::exists(cover_path)) {
        std::cerr << "Failed to generate cover fixture.\n";
        fs::remove_all(root, ec);
        return 1;
    }

    const fs::path audio_path = root / "Artist" / "Album" / "artwork.mp3";
    const std::vector<std::string> audio_args{
        "-loglevel", "error",
        "-nostdin",
        "-f", "lavfi",
        "-i", "sine=frequency=1000:duration=1",
        "-i", cover_path.string(),
        "-map", "0:a",
        "-map", "1:v",
        "-c:a", "libmp3lame",
        "-c:v", "mjpeg",
        "-id3v2_version", "3",
        "-metadata", "title=Artwork Song",
        "-metadata", "artist=Artwork Artist",
        "-metadata", "album=Artwork Album",
        "-metadata:s:v", "title=Album cover",
        "-metadata:s:v", "comment=Cover (front)",
        "-y",
        audio_path.string()};
    if (!test_media_fixture::runCommand(*ffmpeg_path, audio_args) || !fs::exists(audio_path)) {
        std::cerr << "Failed to generate artwork-tagged audio fixture.\n";
        fs::remove_all(root, ec);
        return 1;
    }

    auto services = lofibox::app::withNullRuntimeServices();
    auto cache = std::make_shared<lofibox::platform::host::runtime_detail::SharedRuntimeCache>();
    auto connectivity = std::make_shared<OfflineConnectivityProvider>();
    auto tag_writer = std::make_shared<NoopTagWriter>();
    services.connectivity.provider = connectivity;
    services.metadata.metadata_provider = std::make_shared<FixtureMetadataProvider>();
    services.metadata.artwork_provider = lofibox::platform::host::createHostArtworkProvider(cache, connectivity, tag_writer);
    services.metadata.tag_writer = tag_writer;
    services.playback.audio_backend = std::make_shared<FakeAudioBackend>();

    lofibox::app::LoFiBoxApp app{{root}, {}, std::move(services)};
    app.update();
    for (int tick = 0; tick < 500; ++tick) {
        app.update();
        if (app.snapshot().library_ready) {
            break;
        }
    }

    app.handleInput(lofibox::app::InputEvent{lofibox::app::InputKey::Home, "HOME", '\0'});
    app.handleInput(lofibox::app::InputEvent{lofibox::app::InputKey::Enter, "OK", '\0'});
    app.handleInput(lofibox::app::InputEvent{lofibox::app::InputKey::Enter, "OK", '\0'});

    auto snapshot = app.snapshot();
    if (snapshot.current_page != lofibox::app::AppPage::NowPlaying) {
        std::cerr << "Expected Songs confirm to open Now Playing.\n";
        fs::remove_all(root, ec);
        return 1;
    }

    int red_dominant_pixels = 0;
    for (int attempt = 0; attempt < 300; ++attempt) {
        lofibox::core::Canvas canvas{lofibox::core::kDisplayWidth, lofibox::core::kDisplayHeight};
        app.update();
        app.render(canvas);
        red_dominant_pixels = countRedCoverPixels(canvas);
        if (red_dominant_pixels >= 900) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds{100});
    }

    if (red_dominant_pixels < 900) {
        std::cerr << "Expected Now Playing cover region to show extracted artwork.\n";
        fs::remove_all(root, ec);
        return 1;
    }

    fs::remove_all(root, ec);
    return 0;
}
