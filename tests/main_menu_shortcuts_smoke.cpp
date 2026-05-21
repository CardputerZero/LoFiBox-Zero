// SPDX-License-Identifier: GPL-3.0-or-later

#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <utility>

#include "app/input_event.h"
#include "app/lofibox_app.h"
#include "app/runtime_services.h"

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

void touchFile(const fs::path& path)
{
    std::ofstream output(path.string(), std::ios::binary);
    output << "test";
}

} // namespace

int main()
{
    const fs::path root = fs::temp_directory_path() / "lofibox_zero_main_menu_shortcuts_smoke";
    std::error_code ec{};
    fs::remove_all(root, ec);
    fs::create_directories(root / "Artist" / "Album");
    touchFile(root / "Artist" / "Album" / "alpha.mp3");
    touchFile(root / "Artist" / "Album" / "beta.mp3");

    auto services = lofibox::app::withNullRuntimeServices();
    services.playback.audio_backend = std::make_shared<FakeAudioBackend>();
    lofibox::app::LoFiBoxApp app{{root}, {}, std::move(services)};
    app.update();
    for (int tick = 0; tick < 500; ++tick) {
        app.update();
        if (app.snapshot().library_ready) {
            break;
        }
    }

    auto snapshot = app.snapshot();
    if (snapshot.current_page != lofibox::app::AppPage::MainMenu || snapshot.track_count != 2) {
        std::cerr << "Expected Main Menu with two indexed tracks.\n";
        return 1;
    }

    app.handleInput(lofibox::app::InputEvent{lofibox::app::InputKey::F2, "F2", '\0'});
    snapshot = app.snapshot();
    if (snapshot.playback_status != lofibox::app::PlaybackStatus::Playing || !snapshot.current_track_id) {
        std::cerr << "Expected Main Menu F2 to start or resume playback.\n";
        return 1;
    }

    const int first_id = *snapshot.current_track_id;
    app.handleInput(lofibox::app::InputEvent{lofibox::app::InputKey::F3, "F3", '\0'});
    snapshot = app.snapshot();
    if (snapshot.playback_status != lofibox::app::PlaybackStatus::Paused) {
        std::cerr << "Expected Main Menu F3 to pause playback.\n";
        return 1;
    }

    app.handleInput(lofibox::app::InputEvent{lofibox::app::InputKey::F2, "F2", '\0'});
    app.handleInput(lofibox::app::InputEvent{lofibox::app::InputKey::F5, "F5", '\0'});
    snapshot = app.snapshot();
    if (!snapshot.current_track_id || *snapshot.current_track_id == first_id) {
        std::cerr << "Expected Main Menu F5 to advance to the next track.\n";
        return 1;
    }

    app.handleInput(lofibox::app::InputEvent{lofibox::app::InputKey::F4, "F4", '\0'});
    snapshot = app.snapshot();
    if (!snapshot.current_track_id || *snapshot.current_track_id != first_id) {
        std::cerr << "Expected Main Menu F4 to return to the previous track.\n";
        return 1;
    }

    app.handleInput(lofibox::app::InputEvent{lofibox::app::InputKey::F6, "F6", '\0'});
    snapshot = app.snapshot();
    if (!snapshot.shuffle_enabled || snapshot.repeat_one || snapshot.repeat_all) {
        std::cerr << "Expected Main Menu F6 to cycle from order to shuffle mode.\n";
        return 1;
    }

    app.handleInput(lofibox::app::InputEvent{lofibox::app::InputKey::F6, "F6", '\0'});
    snapshot = app.snapshot();
    if (snapshot.shuffle_enabled || snapshot.repeat_one || !snapshot.repeat_all) {
        std::cerr << "Expected second Main Menu F6 press to cycle to repeat-all mode.\n";
        return 1;
    }

    app.handleInput(lofibox::app::InputEvent{lofibox::app::InputKey::F6, "F6", '\0'});
    snapshot = app.snapshot();
    if (snapshot.shuffle_enabled || snapshot.repeat_all || !snapshot.repeat_one) {
        std::cerr << "Expected third Main Menu F6 press to cycle to repeat-one mode.\n";
        return 1;
    }

    app.handleInput(lofibox::app::InputEvent{lofibox::app::InputKey::F6, "F6", '\0'});
    snapshot = app.snapshot();
    if (snapshot.shuffle_enabled || snapshot.repeat_one || snapshot.repeat_all) {
        std::cerr << "Expected fourth Main Menu F6 press to return to order mode.\n";
        return 1;
    }

    app.handleInput(lofibox::app::InputEvent{lofibox::app::InputKey::F7, "F7", '\0'});
    app.handleInput(lofibox::app::InputEvent{lofibox::app::InputKey::F8, "F8", '\0'});
    snapshot = app.snapshot();
    if (snapshot.shuffle_enabled || snapshot.repeat_one || snapshot.repeat_all) {
        std::cerr << "Expected Main Menu F7/F8 not to mutate the unified playback mode.\n";
        return 1;
    }

    fs::remove_all(root, ec);
    return 0;
}
