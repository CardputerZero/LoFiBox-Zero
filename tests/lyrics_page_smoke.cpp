// SPDX-License-Identifier: GPL-3.0-or-later

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <thread>

#include "app/input_event.h"
#include "app/lofibox_app.h"
#include "app/runtime_services.h"
#include "core/canvas.h"
#include "core/display_profile.h"

namespace fs = std::filesystem;

namespace {

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
    bool playUri(const std::string&, double) override
    {
        playing = true;
        paused = false;
        return true;
    }
    void stop() override
    {
        playing = false;
        paused = false;
    }
    void pause() override { paused = true; }
    void resume() override
    {
        playing = true;
        paused = false;
    }
    [[nodiscard]] bool isPlaying() override { return playing && !paused; }
    [[nodiscard]] bool isFinished() override { return false; }

private:
    bool playing{false};
    bool paused{false};
};

class FixedLyricsProvider final : public lofibox::app::LyricsProvider {
public:
    [[nodiscard]] bool available() const override { return true; }
    [[nodiscard]] std::string displayName() const override { return "TEST"; }

    [[nodiscard]] lofibox::app::TrackLyrics fetch(const fs::path&, const lofibox::app::TrackMetadata&) const override
    {
        lofibox::app::TrackLyrics lyrics{};
        lyrics.synced = "[00:00.00]First line\n[00:05.00]Second line\n[00:10.00]Third line";
        lyrics.plain = "First line\nSecond line\nThird line";
        lyrics.source = "TEST";
        return lyrics;
    }
};

void touchFile(const fs::path& path)
{
    std::ofstream output(path.string(), std::ios::binary);
    output << "test";
}

} // namespace

int main()
{
    const fs::path root = fs::temp_directory_path() / "lofibox_zero_lyrics_page_smoke";
    std::error_code ec{};
    fs::remove_all(root, ec);
    fs::create_directories(root, ec);
    touchFile(root / "lyrics-track.mp3");

    lofibox::app::RuntimeServices services{};
    services.playback.audio_backend = std::make_shared<FakeAudioBackend>();
    services.metadata.lyrics_provider = std::make_shared<FixedLyricsProvider>();

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

    for (int index = 0; index < 8; ++index) {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        app.update();
    }

    auto snapshot = app.snapshot();
    if (snapshot.current_page != lofibox::app::AppPage::NowPlaying) {
        std::cerr << "Expected playback to open Now Playing.\n";
        fs::remove_all(root, ec);
        return 1;
    }

    app.handleInput(lofibox::app::makeCharacterInput('l', "L"));
    snapshot = app.snapshot();
    if (snapshot.current_page != lofibox::app::AppPage::Lyrics) {
        std::cerr << "Expected L on Now Playing to open Lyrics.\n";
        fs::remove_all(root, ec);
        return 1;
    }

    lofibox::core::Canvas canvas{lofibox::core::kDisplayWidth, lofibox::core::kDisplayHeight};
    app.render(canvas);

    int highlight_pixels = 0;
    for (int y = 84; y < 112; ++y) {
        for (int x = 34; x < 286; ++x) {
            const auto pixel = canvas.pixel(x, y);
            if (pixel.b > 60 && pixel.g > 25 && pixel.r < 40) {
                ++highlight_pixels;
            }
        }
    }

    if (highlight_pixels < 100) {
        std::cerr << "Expected Lyrics page to center and highlight the active lyric row.\n";
        fs::remove_all(root, ec);
        return 1;
    }

    app.handleInput(lofibox::app::makeCharacterInput('L', "L"));
    snapshot = app.snapshot();
    if (snapshot.current_page != lofibox::app::AppPage::NowPlaying) {
        std::cerr << "Expected L on Lyrics to return to Now Playing.\n";
        fs::remove_all(root, ec);
        return 1;
    }

    fs::remove_all(root, ec);
    return 0;
}
