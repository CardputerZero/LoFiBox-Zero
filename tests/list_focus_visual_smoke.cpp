// SPDX-License-Identifier: GPL-3.0-or-later

#include <cmath>
#include <iostream>

#include "ui/pages/list_page.h"
#include "core/canvas.h"
#include "core/display_profile.h"

namespace {

int brightness(lofibox::core::Color color)
{
    return static_cast<int>(color.r) + static_cast<int>(color.g) + static_cast<int>(color.b);
}

} // namespace

int main()
{
    lofibox::core::Canvas canvas{lofibox::core::kDisplayWidth, lofibox::core::kDisplayHeight};
    lofibox::ui::pages::ListPageView view{};
    view.title = "Songs";
    view.rows = {
        {"Alpha", "1:00"},
        {"Beta", "2:00"},
        {"Gamma", "3:00"},
        {"Delta", "4:00"},
        {"Epsilon", "5:00"},
        {"Zeta", "6:00"},
        {"Eta", "7:00"},
        {"Theta", "8:00"},
        {"Iota", "9:00"},
        {"Kappa", "10:00"},
    };
    view.selected = 1;

    lofibox::ui::pages::renderListPage(canvas, view, lofibox::ui::defaultTheme());

    constexpr int row_y = 28 + 21;
    constexpr int sample_x = 160;
    const int top = brightness(canvas.pixel(sample_x, row_y + 1));
    const int center = brightness(canvas.pixel(sample_x, row_y + 10));
    const int bottom = brightness(canvas.pixel(sample_x, row_y + 18));

    if (center <= top + 70 || center <= bottom + 70) {
        std::cerr << "Expected selected list focus to fade from center toward row edges.\n";
        return 1;
    }

    if (std::abs(top - bottom) > 80) {
        std::cerr << "Expected selected list focus to have balanced top/bottom fade.\n";
        return 1;
    }

    constexpr int track_x = 313;
    constexpr int thumb_top_y = 28;
    const int thumb_top = brightness(canvas.pixel(track_x, thumb_top_y + 1));
    const int thumb_middle = brightness(canvas.pixel(track_x, thumb_top_y + 37));
    const int thumb_bottom = brightness(canvas.pixel(track_x, thumb_top_y + 72));
    if (thumb_middle <= thumb_top + 45 || thumb_middle <= thumb_bottom + 20) {
        std::cerr << "Expected list scrollbar thumb to use a vertical glass fade.\n";
        return 1;
    }

    return 0;
}
