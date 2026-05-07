// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui/pages/main_menu_page.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <string>

#include "ui/ui_primitives.h"
#include "core/bitmap_font.h"
#include "core/display_profile.h"

namespace lofibox::ui::pages {
namespace {

using core::rgba;

struct MenuCardTheme {
    core::Color glow{};
    core::Color border{};
    core::Color top{};
    core::Color bottom{};
    core::Color shine{};
    core::Color label{};
    core::Color icon_primary{};
    core::Color icon_secondary{};
    core::Color icon_tertiary{};
};

void drawMainMenuTopbar(core::Canvas& canvas, const MainMenuView& view, const UiTheme& theme)
{
    canvas.fillRect(0, 0, core::kDisplayWidth, 20, theme.palette.background);
    canvas.fillRect(0, 0, core::kDisplayWidth, 14, theme.palette.chrome_topbar0);
    canvas.fillRect(0, 13, core::kDisplayWidth, 7, theme.palette.chrome_topbar1);
    canvas.fillRect(0, 19, core::kDisplayWidth, 1, theme.palette.divider);

    const int text_y = ::lofibox::ui::centeredTextY(0, theme.spacing.top_bar_height, 1);
    ::lofibox::ui::drawText(canvas, "F1:HELP", 6, text_y, theme.palette.text_secondary, 1);

    constexpr int kInfoX = 118;
    constexpr int kInfoWidth = core::kDisplayWidth - kInfoX - 8;
    const auto text_color = view.playback_summary == "NO TRACK" ? theme.palette.text_muted : theme.palette.text_primary;
    ::lofibox::ui::drawFadedTextWindow(canvas, view.playback_summary, kInfoX, text_y, kInfoWidth, view.playback_summary_scroll_px, text_color);
}

void drawMusicIcon(core::Canvas& canvas, int x, int y, int width, int height, const MenuCardTheme& card_theme)
{
    const int stem_x = x + width / 2 - 2;
    const int stem_y = y + 14;
    ::lofibox::ui::drawLine(canvas, stem_x, stem_y, stem_x, y + height - 24, card_theme.icon_primary, 3);
    ::lofibox::ui::drawLine(canvas, stem_x, stem_y, x + width - 20, y + 8, card_theme.icon_primary, 3);
    ::lofibox::ui::drawLine(canvas, x + width - 18, y + 8, x + width - 18, y + height - 30, card_theme.icon_primary, 3);
    canvas.fillRoundedRect(x + 12, y + height - 30, 20, 20, 10, card_theme.icon_primary);
    canvas.fillRoundedRect(x + width - 30, y + height - 36, 20, 20, 10, card_theme.icon_primary);
}

void drawFolderIcon(core::Canvas& canvas, int x, int y, int width, int height, const MenuCardTheme& card_theme, const UiTheme& theme)
{
    canvas.fillRoundedRect(x + 8, y + 18, width - 18, height - 30, 6, card_theme.icon_secondary);
    canvas.fillRoundedRect(x + 14, y + 10, width / 2, 18, 6, card_theme.icon_primary);
    canvas.fillRoundedRect(x + 8, y + 22, width - 18, height - 32, 6, card_theme.icon_primary);
    canvas.strokeRect(x + 8, y + 22, width - 18, height - 32, ::lofibox::ui::mixColor(card_theme.icon_primary, theme.palette.text_primary, 0.25f), 1);
}

void drawPlaylistsIcon(core::Canvas& canvas, int x, int y, int width, int height, const MenuCardTheme& card_theme, const UiTheme& theme)
{
    canvas.fillRoundedRect(x + 8, y + 20, width - 24, height - 34, 6, card_theme.icon_secondary);
    canvas.fillRoundedRect(x + 16, y + 10, width - 24, height - 34, 6, card_theme.icon_tertiary);
    canvas.fillRoundedRect(x + 26, y + 20, width - 26, height - 30, 6, card_theme.icon_primary);
    canvas.fillRect(x + 36, y + 32, width - 46, 4, ::lofibox::ui::mixColor(theme.palette.text_primary, card_theme.icon_primary, 0.4f));
    canvas.fillRect(x + 36, y + 44, width - 50, 4, ::lofibox::ui::mixColor(theme.palette.text_primary, card_theme.icon_primary, 0.4f));
    canvas.fillRect(x + 36, y + 56, width - 54, 4, ::lofibox::ui::mixColor(theme.palette.text_primary, card_theme.icon_primary, 0.4f));
}

void drawMenuCard(
    core::Canvas& canvas,
    int x,
    int y,
    int width,
    int height,
    std::string_view label,
    const MenuCardTheme& card_theme,
    std::string_view kind,
    const core::Canvas* icon_canvas,
    const UiTheme& theme)
{
    if (icon_canvas != nullptr) {
        ::lofibox::ui::blitScaledCanvas(canvas, *icon_canvas, x, y, width, height, 255);
    } else if (kind == "MUSIC") {
        drawMusicIcon(canvas, x, y, width, height, card_theme);
    } else if (kind == "LIBRARY") {
        drawFolderIcon(canvas, x, y, width, height, card_theme, theme);
    } else if (kind == "PLAYLISTS") {
        drawPlaylistsIcon(canvas, x, y, width, height, card_theme, theme);
    } else {
        canvas.fillRoundedRect(x + 8, y + 10, width - 16, height - 20, 6, card_theme.icon_primary);
        ::lofibox::ui::drawText(canvas, ::lofibox::ui::fitUpper(label, 8), x + 12, y + height / 2 - 4, theme.palette.text_primary, 1);
    }
}

void drawMenuLabelCentered(core::Canvas& canvas, std::string_view label, int center_x, int y, core::Color color)
{
    const auto fitted = ::lofibox::ui::fitUpper(label, 10);
    const int width = core::bitmap_font::measureText(fitted, 1);
    ::lofibox::ui::drawText(canvas, fitted, center_x - (width / 2), y, color, 1);
}

} // namespace

void renderMainMenuPage(core::Canvas& canvas, const MainMenuView& view, const UiTheme& theme)
{
    constexpr std::array<std::string_view, 6> kItems{"MUSIC", "LIBRARY", "PLAYLISTS", "NOW", "EQ", "SETTINGS"};
    constexpr int kLeftCenterX = 56;
    constexpr int kCenterCardX = 112;
    constexpr int kCenterCenterX = 160;
    constexpr int kRightCenterX = 264;
    constexpr int kSideY = 38;
    constexpr int kCenterY = 28;
    constexpr int kSideSize = 68;
    constexpr int kCenterSize = 96;

    const auto theme_for = [&](int index) {
        const std::array<MenuCardTheme, 6> kThemes{
            MenuCardTheme{rgba(87, 78, 255), rgba(80, 143, 255), rgba(64, 168, 255), rgba(32, 52, 198), rgba(191, 238, 255), theme.palette.text_primary, rgba(235, 245, 255), rgba(96, 174, 255), rgba(54, 69, 225)},
            MenuCardTheme{rgba(0, 214, 255), rgba(89, 216, 255), rgba(96, 229, 246), rgba(43, 124, 224), rgba(214, 255, 255), theme.palette.text_primary, rgba(80, 209, 228), rgba(64, 171, 223), rgba(90, 238, 255)},
            MenuCardTheme{rgba(255, 75, 126), rgba(255, 205, 80), rgba(255, 120, 155), rgba(97, 40, 222), rgba(255, 238, 165), theme.palette.text_primary, rgba(253, 118, 67), rgba(126, 77, 255), rgba(255, 82, 156)},
            MenuCardTheme{rgba(66, 220, 140), rgba(101, 230, 170), rgba(89, 226, 166), rgba(34, 117, 84), rgba(210, 255, 228), theme.palette.text_primary, rgba(112, 236, 178), rgba(44, 143, 96), rgba(126, 255, 213)},
            MenuCardTheme{rgba(255, 178, 87), rgba(255, 127, 42), rgba(255, 192, 92), rgba(177, 73, 33), rgba(255, 240, 196), theme.palette.text_primary, rgba(255, 178, 87), rgba(255, 127, 42), rgba(255, 222, 127)},
            MenuCardTheme{rgba(172, 124, 255), rgba(143, 101, 255), rgba(172, 124, 255), rgba(78, 55, 173), rgba(226, 215, 255), theme.palette.text_primary, rgba(172, 124, 255), rgba(94, 217, 255), rgba(255, 112, 196)},
        };
        return kThemes[static_cast<std::size_t>(std::clamp(index, 0, static_cast<int>(kThemes.size()) - 1))];
    };

    const auto icon_for = [&](int index) -> const core::Canvas* {
        if (view.assets == nullptr) {
            return nullptr;
        }
        switch (index) {
        case 0: return view.assets->music_icon ? &*view.assets->music_icon : nullptr;
        case 1: return view.assets->library_icon ? &*view.assets->library_icon : nullptr;
        case 2: return view.assets->playlists_icon ? &*view.assets->playlists_icon : nullptr;
        case 3: return view.assets->now_playing_icon ? &*view.assets->now_playing_icon : nullptr;
        case 4: return view.assets->equalizer_icon ? &*view.assets->equalizer_icon : nullptr;
        case 5: return view.assets->settings_icon ? &*view.assets->settings_icon : nullptr;
        default: return nullptr;
        }
    };

    canvas.fillRect(0, 0, core::kDisplayWidth, core::kDisplayHeight, theme.palette.background);
    drawMainMenuTopbar(canvas, view, theme);

    const auto wrap_index = [&](int index) {
        const int count = static_cast<int>(kItems.size());
        return ((index % count) + count) % count;
    };
    const int selected = wrap_index(view.selected);
    const int left_index = wrap_index(selected - 1);
    const int right_index = wrap_index(selected + 1);

    drawMenuCard(canvas, kLeftCenterX - (kSideSize / 2), kSideY, kSideSize, kSideSize, kItems[static_cast<std::size_t>(left_index)], theme_for(left_index), kItems[static_cast<std::size_t>(left_index)], icon_for(left_index), theme);
    drawMenuLabelCentered(canvas, kItems[static_cast<std::size_t>(left_index)], kLeftCenterX, 120, theme.palette.text_secondary);

    drawMenuCard(canvas, kCenterCardX, kCenterY, kCenterSize, kCenterSize, kItems[static_cast<std::size_t>(selected)], theme_for(selected), kItems[static_cast<std::size_t>(selected)], icon_for(selected), theme);
    drawMenuLabelCentered(canvas, kItems[static_cast<std::size_t>(selected)], kCenterCenterX, 126, theme.palette.text_primary);

    drawMenuCard(canvas, kRightCenterX - (kSideSize / 2), kSideY, kSideSize, kSideSize, kItems[static_cast<std::size_t>(right_index)], theme_for(right_index), kItems[static_cast<std::size_t>(right_index)], icon_for(right_index), theme);
    drawMenuLabelCentered(canvas, kItems[static_cast<std::size_t>(right_index)], kRightCenterX, 120, theme.palette.text_secondary);

    ::lofibox::ui::drawPaginationDots(canvas, theme, 135, 152, static_cast<int>(kItems.size()), selected);
}

} // namespace lofibox::ui::pages
