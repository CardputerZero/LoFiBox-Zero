// SPDX-License-Identifier: GPL-3.0-or-later

#include <iostream>
#include <thread>
#include <unordered_set>

#include "app/lofibox_app.h"
#include "core/canvas.h"
#include "core/color.h"
#include "core/display_profile.h"
#include "platform/host/legacy_asset_loader.h"
#include "ui/pages/main_menu_page.h"

int main()
{
    auto assets = lofibox::platform::host::loadLegacyAssets();
    lofibox::app::LoFiBoxApp app{{}, std::move(assets)};
    lofibox::core::Canvas canvas{lofibox::core::kDisplayWidth, lofibox::core::kDisplayHeight};

    for (int index = 0; index < 9; ++index) {
        app.update();
        std::this_thread::sleep_for(std::chrono::milliseconds(220));
    }
    app.render(canvas);

    const auto snapshot = app.snapshot();
    if (snapshot.current_page != lofibox::app::AppPage::MainMenu) {
        std::cerr << "Expected Main Menu to be active for the visual smoke test.\n";
        return 1;
    }

    const auto background = canvas.pixel(0, 0);

    const auto selected_dot = canvas.pixel(145, 152);

    auto iconHasContent = [&](int x, int y, int w, int h) {
        std::size_t non_background = 0;
        std::unordered_set<std::uint32_t> palette{};
        for (int py = y; py < y + h; ++py) {
            for (int px = x; px < x + w; ++px) {
                const auto pixel = canvas.pixel(px, py);
                if (pixel != background) {
                    ++non_background;
                }
                palette.insert(lofibox::core::pack_key(pixel));
            }
        }
        return non_background > 1200 && palette.size() > 8;
    };

    auto regionHasContent = [&](int x, int y, int w, int h) {
        std::size_t non_background = 0;
        for (int py = y; py < y + h; ++py) {
            for (int px = x; px < x + w; ++px) {
                if (canvas.pixel(px, py) != background) {
                    ++non_background;
                }
            }
        }
        return non_background > 100;
    };

    auto regionIsBackgroundOnly = [&](int x, int y, int w, int h) {
        for (int py = y; py < y + h; ++py) {
            for (int px = x; px < x + w; ++px) {
                if (canvas.pixel(px, py) != background) {
                    return false;
                }
            }
        }
        return true;
    };

    if (regionIsBackgroundOnly(22, 38, 68, 68) || regionIsBackgroundOnly(230, 38, 68, 68)) {
        std::cerr << "Expected Main Menu side preview regions to be populated.\n";
        return 1;
    }

    if (!regionHasContent(6, 5, 46, 12)) {
        std::cerr << "Expected F1:HELP hint on the Main Menu topbar.\n";
        return 1;
    }

    if (!regionHasContent(118, 5, 194, 12)) {
        std::cerr << "Expected playback summary on the Main Menu topbar.\n";
        return 1;
    }

    auto topbarTextDiffPixels = [](const lofibox::core::Canvas& a, const lofibox::core::Canvas& b, int x, int y, int w, int h) {
        int diff = 0;
        for (int py = y; py < y + h; ++py) {
            for (int px = x; px < x + w; ++px) {
                if (a.pixel(px, py) != b.pixel(px, py)) {
                    ++diff;
                }
            }
        }
        return diff;
    };
    lofibox::ui::pages::MainMenuView empty_summary_view{};
    empty_summary_view.playback_summary = "";
    lofibox::core::Canvas empty_summary_canvas{lofibox::core::kDisplayWidth, lofibox::core::kDisplayHeight};
    lofibox::ui::pages::renderMainMenuPage(empty_summary_canvas, empty_summary_view, lofibox::ui::defaultTheme());

    lofibox::ui::pages::MainMenuView short_summary_view{};
    short_summary_view.playback_summary = "NO TRACK";
    short_summary_view.playback_summary_scroll_px = 0;
    lofibox::core::Canvas short_summary_canvas{lofibox::core::kDisplayWidth, lofibox::core::kDisplayHeight};
    lofibox::ui::pages::renderMainMenuPage(short_summary_canvas, short_summary_view, lofibox::ui::defaultTheme());
    const int short_left_diff = topbarTextDiffPixels(short_summary_canvas, empty_summary_canvas, 118, 3, 70, 15);
    const int short_right_diff = topbarTextDiffPixels(short_summary_canvas, empty_summary_canvas, 230, 3, 82, 15);
    if (short_right_diff <= short_left_diff || short_right_diff < 12) {
        std::cerr << "Expected fitting Main Menu playback summary text to be right-aligned in its fixed topbar region.\n";
        return 1;
    }

    lofibox::ui::pages::MainMenuView long_summary_view{};
    long_summary_view.playback_summary = "PLAYING  VERY LONG REMOTE TRACK TITLE - VERY LONG ARTIST NAME";
    long_summary_view.playback_summary_scroll_px = 0;
    lofibox::core::Canvas long_summary_start{lofibox::core::kDisplayWidth, lofibox::core::kDisplayHeight};
    lofibox::ui::pages::renderMainMenuPage(long_summary_start, long_summary_view, lofibox::ui::defaultTheme());
    long_summary_view.playback_summary_scroll_px = 54;
    lofibox::core::Canvas long_summary_scrolled{lofibox::core::kDisplayWidth, lofibox::core::kDisplayHeight};
    lofibox::ui::pages::renderMainMenuPage(long_summary_scrolled, long_summary_view, lofibox::ui::defaultTheme());
    if (topbarTextDiffPixels(long_summary_start, long_summary_scrolled, 118, 3, 194, 15) < 20) {
        std::cerr << "Expected overflowing Main Menu playback summary to scroll right-to-left in the fixed topbar region.\n";
        return 1;
    }

    app.handleInput(lofibox::app::InputEvent{lofibox::app::InputKey::F1, "F1", '\0'});
    lofibox::core::Canvas help_canvas{lofibox::core::kDisplayWidth, lofibox::core::kDisplayHeight};
    app.render(help_canvas);
    if (canvas.pixel(30, 30) == help_canvas.pixel(30, 30) || canvas.pixel(160, 38) == help_canvas.pixel(160, 38)) {
        std::cerr << "Expected Main Menu F1 to render a page-owned shortcuts modal.\n";
        return 1;
    }
    app.handleInput(lofibox::app::InputEvent{lofibox::app::InputKey::Backspace, "BACK", '\0'});
    app.render(canvas);

    if (selected_dot == background) {
        std::cerr << "Expected visible pagination dots on Main Menu.\n";
        return 1;
    }

    auto requireSelectedIcon = [&](const char* name) {
        if (!iconHasContent(112, 28, 96, 96)) {
            std::cerr << "Expected the selected Main Menu card for " << name << " to contain real iconography.\n";
            return false;
        }
        return true;
    };

    if (!iconHasContent(22, 38, 68, 68)) {
        std::cerr << "Expected the left preview card to contain real iconography.\n";
        return 1;
    }

    if (!requireSelectedIcon("Library")) {
        return 1;
    }

    if (!iconHasContent(230, 38, 68, 68)) {
        std::cerr << "Expected the right preview card to contain real iconography.\n";
        return 1;
    }

    app.handleInput(lofibox::app::InputEvent{lofibox::app::InputKey::Home, "HOME", '\0'});
    app.render(canvas);
    if (!regionHasContent(22, 38, 68, 68) || !regionHasContent(230, 38, 68, 68)) {
        std::cerr << "Expected Music selection to wrap with populated Settings/Music-adjacent previews.\n";
        return 1;
    }
    app.handleInput(lofibox::app::InputEvent{lofibox::app::InputKey::Left, "LEFT", '\0'});
    app.render(canvas);
    if (!requireSelectedIcon("Settings") || !regionHasContent(230, 38, 68, 68)) {
        std::cerr << "Expected Left from Music to wrap to Settings with Music as right preview.\n";
        return 1;
    }
    app.handleInput(lofibox::app::InputEvent{lofibox::app::InputKey::Home, "HOME", '\0'});
    app.handleInput(lofibox::app::InputEvent{lofibox::app::InputKey::Right, "RIGHT", '\0'});
    app.render(canvas);

    const struct MenuStep {
        lofibox::app::InputKey key;
        const char* label;
        const char* name;
    } steps[] = {
        {lofibox::app::InputKey::Right, "RIGHT", "Playlists"},
        {lofibox::app::InputKey::Right, "RIGHT", "Now Playing"},
        {lofibox::app::InputKey::Right, "RIGHT", "Equalizer"},
        {lofibox::app::InputKey::Right, "RIGHT", "Settings"},
    };

    for (const auto& step : steps) {
        app.handleInput(lofibox::app::InputEvent{step.key, step.label, '\0'});
        app.render(canvas);
        if (!requireSelectedIcon(step.name)) {
            return 1;
        }
    }

    return 0;
}
