// SPDX-License-Identifier: GPL-3.0-or-later

#include <filesystem>
#include <fstream>
#include <iostream>

#include "app/input_event.h"
#include "app/lofibox_app.h"
#include "core/canvas.h"
#include "core/display_profile.h"

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
    const fs::path root = fs::temp_directory_path() / "lofibox_zero_list_help_modal_smoke";
    std::error_code ec{};
    fs::remove_all(root, ec);
    fs::create_directories(root / "Artist" / "Album");
    touchFile(root / "Artist" / "Album" / "alpha.mp3");
    touchFile(root / "Artist" / "Album" / "beta.mp3");

    lofibox::app::LoFiBoxApp app{{root}};
    app.update();
    for (int tick = 0; tick < 500; ++tick) {
        app.update();
        if (app.snapshot().library_ready) {
            break;
        }
    }

    app.handleInput(lofibox::app::InputEvent{lofibox::app::InputKey::Enter, "OK", '\0'});
    app.handleInput(lofibox::app::InputEvent{lofibox::app::InputKey::Down, "DOWN", '\0'});
    app.handleInput(lofibox::app::InputEvent{lofibox::app::InputKey::Down, "DOWN", '\0'});
    app.handleInput(lofibox::app::InputEvent{lofibox::app::InputKey::Enter, "OK", '\0'});
    if (app.snapshot().current_page != lofibox::app::AppPage::Songs) {
        std::cerr << "Expected test setup to enter Songs page.\n";
        return 1;
    }

    lofibox::core::Canvas before{lofibox::core::kDisplayWidth, lofibox::core::kDisplayHeight};
    lofibox::core::Canvas after{lofibox::core::kDisplayWidth, lofibox::core::kDisplayHeight};
    app.render(before);
    app.handleInput(lofibox::app::InputEvent{lofibox::app::InputKey::F1, "F1", '\0'});
    app.render(after);

    if (before.pixel(30, 30) == after.pixel(30, 30) || before.pixel(160, 38) == after.pixel(160, 38)) {
        std::cerr << "Expected F1 to render a shortcut help modal over the Songs list.\n";
        return 1;
    }

    app.handleInput(lofibox::app::InputEvent{lofibox::app::InputKey::Backspace, "BACK", '\0'});
    lofibox::core::Canvas closed{lofibox::core::kDisplayWidth, lofibox::core::kDisplayHeight};
    app.render(closed);
    if (closed.pixel(30, 30) == after.pixel(30, 30)) {
        std::cerr << "Expected Backspace to close the shortcut help modal.\n";
        return 1;
    }

    fs::remove_all(root, ec);
    return 0;
}
