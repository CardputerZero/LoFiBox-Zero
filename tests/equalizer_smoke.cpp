// SPDX-License-Identifier: GPL-3.0-or-later

#include <iostream>

#include "app/input_event.h"
#include "app/lofibox_app.h"
#include "core/canvas.h"
#include "core/display_profile.h"

namespace {

int changedPixels(
    const lofibox::core::Canvas& before,
    const lofibox::core::Canvas& after,
    int x,
    int y,
    int width,
    int height)
{
    int changed = 0;
    for (int row = y; row < y + height; ++row) {
        for (int col = x; col < x + width; ++col) {
            if (before.pixel(col, row) != after.pixel(col, row)) {
                ++changed;
            }
        }
    }
    return changed;
}

} // namespace

int main()
{
    lofibox::app::LoFiBoxApp app{};
    app.update();
    for (int tick = 0; tick < 500; ++tick) {
        app.update();
        if (app.snapshot().library_ready) {
            break;
        }
    }

    app.handleInput(lofibox::app::InputEvent{lofibox::app::InputKey::Right, "RIGHT", '\0'});
    app.handleInput(lofibox::app::InputEvent{lofibox::app::InputKey::Right, "RIGHT", '\0'});
    app.handleInput(lofibox::app::InputEvent{lofibox::app::InputKey::Right, "RIGHT", '\0'});
    app.handleInput(lofibox::app::InputEvent{lofibox::app::InputKey::Enter, "OK", '\0'});

    auto snapshot = app.snapshot();
    if (snapshot.current_page != lofibox::app::AppPage::Equalizer) {
        std::cerr << "Expected Main Menu selection to reach Equalizer.\n";
        return 1;
    }

    if (snapshot.eq_band_count != 10) {
        std::cerr << "Expected Equalizer to expose 10 bands.\n";
        return 1;
    }

    for (int index = 0; index < 9; ++index) {
        app.handleInput(lofibox::app::InputEvent{lofibox::app::InputKey::Right, "RIGHT", '\0'});
    }

    snapshot = app.snapshot();
    if (snapshot.eq_selected_band != 9) {
        std::cerr << "Expected Equalizer selection to reach the 10th band.\n";
        return 1;
    }

    app.handleInput(lofibox::app::InputEvent{lofibox::app::InputKey::Right, "RIGHT", '\0'});
    snapshot = app.snapshot();
    if (snapshot.eq_selected_band != 9) {
        std::cerr << "Expected Equalizer selection to stay clamped at the last band.\n";
        return 1;
    }

    lofibox::core::Canvas preset_before{lofibox::core::kDisplayWidth, lofibox::core::kDisplayHeight};
    lofibox::core::Canvas preset_after{lofibox::core::kDisplayWidth, lofibox::core::kDisplayHeight};
    app.render(preset_before);
    app.handleInput(lofibox::app::InputEvent{lofibox::app::InputKey::Enter, "OK", '\0'});
    app.render(preset_after);
    if (changedPixels(preset_before, preset_after, 76, 30, 230, 128) < 12) {
        std::cerr << "Expected Equalizer OK to cycle preset and update gain labels/sliders.\n";
        return 1;
    }

    lofibox::core::Canvas before{lofibox::core::kDisplayWidth, lofibox::core::kDisplayHeight};
    lofibox::core::Canvas after{lofibox::core::kDisplayWidth, lofibox::core::kDisplayHeight};
    app.render(before);
    app.handleInput(lofibox::app::InputEvent{lofibox::app::InputKey::F1, "F1", '\0'});
    app.render(after);
    if (before.pixel(30, 30) == after.pixel(30, 30) || before.pixel(160, 82) == after.pixel(160, 82)) {
        std::cerr << "Expected Equalizer F1 to render its page-owned shortcut help modal.\n";
        return 1;
    }

    app.handleInput(lofibox::app::InputEvent{lofibox::app::InputKey::Backspace, "BACK", '\0'});
    lofibox::core::Canvas closed{lofibox::core::kDisplayWidth, lofibox::core::kDisplayHeight};
    app.render(closed);
    if (closed.pixel(30, 30) == after.pixel(30, 30)) {
        std::cerr << "Expected Backspace to close the Equalizer help modal.\n";
        return 1;
    }

    return 0;
}
