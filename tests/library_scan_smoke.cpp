// SPDX-License-Identifier: GPL-3.0-or-later

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

#include "app/input_event.h"
#include "app/lofibox_app.h"

namespace fs = std::filesystem;

namespace {

void touchFile(const fs::path& path)
{
    std::ofstream output(path.string(), std::ios::binary);
    output << "test";
}

} // namespace

int main()
{
    const fs::path root = fs::temp_directory_path() / "lofibox_zero_library_scan_smoke";
    std::error_code ec{};
    fs::remove_all(root, ec);

    fs::create_directories(root / "ArtistA" / "Shared Album");
    fs::create_directories(root / "ArtistB" / "Shared Album");
    fs::create_directories(root / "ArtistA" / "Solo Album");
    fs::create_directories(root / "ArtistC");

    touchFile(root / "ArtistA" / "Shared Album" / "track1.mp3");
    touchFile(root / "ArtistB" / "Shared Album" / "track2.mp3");
    touchFile(root / "ArtistA" / "Solo Album" / "track3.wav");
    for (int index = 4; index <= 12; ++index) {
        touchFile(root / "ArtistC" / ("track" + std::to_string(index) + ".mp3"));
    }

    lofibox::app::LoFiBoxApp app{{root}};
    app.update();

    for (int tick = 0; tick < 500; ++tick) {
        app.update();
        if (app.snapshot().library_ready) {
            break;
        }
    }

    auto snapshot = app.snapshot();
    if (!snapshot.library_ready) {
        std::cerr << "Expected library to be ready after async scan (500 ticks).\n";
        return 1;
    }

    if (snapshot.track_count != 12) {
        std::cerr << "Expected twelve scanned tracks.\n";
        return 1;
    }

    app.handleInput(lofibox::app::InputEvent{lofibox::app::InputKey::Enter, "OK", '\0'});
    snapshot = app.snapshot();
    if (snapshot.current_page != lofibox::app::AppPage::MusicIndex) {
        std::cerr << "Expected Main Menu -> Library to enter Music Index.\n";
        return 1;
    }

    if (snapshot.visible_count != 6) {
        std::cerr << "Expected six fixed Music Index rows.\n";
        return 1;
    }

    app.handleInput(lofibox::app::InputEvent{lofibox::app::InputKey::Enter, "OK", '\0'});
    snapshot = app.snapshot();
    if (snapshot.current_page != lofibox::app::AppPage::Artists) {
        std::cerr << "Expected Music Index confirm on first row to enter Artists.\n";
        return 1;
    }

    if (snapshot.visible_count != 3) {
        std::cerr << "Expected three unique artists from the scanned library.\n";
        return 1;
    }

    app.handleInput(lofibox::app::InputEvent{lofibox::app::InputKey::Backspace, "DEL", '\0'});
    app.handleInput(lofibox::app::InputEvent{lofibox::app::InputKey::Down, "DOWN", '\0'});
    app.handleInput(lofibox::app::InputEvent{lofibox::app::InputKey::Down, "DOWN", '\0'});
    app.handleInput(lofibox::app::InputEvent{lofibox::app::InputKey::Enter, "OK", '\0'});
    snapshot = app.snapshot();
    if (snapshot.current_page != lofibox::app::AppPage::Songs) {
        std::cerr << "Expected Music Index -> Songs to enter Songs.\n";
        return 1;
    }

    if (snapshot.visible_count != 12) {
        std::cerr << "Expected Songs to expose every scanned track.\n";
        return 1;
    }

    app.handleInput(lofibox::app::InputEvent{lofibox::app::InputKey::Up, "UP", '\0'});
    snapshot = app.snapshot();
    if (snapshot.list_selected_index != 11) {
        std::cerr << "Expected Songs selection to wrap upward from the first track to the final track.\n";
        return 1;
    }

    if (snapshot.list_scroll_offset != 6) {
        std::cerr << "Expected Songs scroll offset to reveal the final page.\n";
        return 1;
    }

    app.handleInput(lofibox::app::InputEvent{lofibox::app::InputKey::Down, "DOWN", '\0'});
    snapshot = app.snapshot();
    if (snapshot.list_selected_index != 0 || snapshot.list_scroll_offset != 0) {
        std::cerr << "Expected Songs selection to wrap downward from the final track back to the first track.\n";
        return 1;
    }

    fs::remove_all(root, ec);
    return 0;
}
