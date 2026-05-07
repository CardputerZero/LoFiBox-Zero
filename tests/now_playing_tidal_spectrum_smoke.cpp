// SPDX-License-Identifier: GPL-3.0-or-later

#include <array>
#include <iostream>

#include "ui/pages/now_playing_page.h"
#include "ui/ui_primitives.h"
#include "core/canvas.h"
#include "core/display_profile.h"

namespace {

[[nodiscard]] bool isBrightSpectrumPixel(lofibox::core::Color pixel)
{
    return pixel.r > 55 || pixel.g > 75 || pixel.b > 95;
}

} // namespace

int main()
{
    lofibox::core::Canvas canvas{lofibox::core::kDisplayWidth, lofibox::core::kDisplayHeight};
    const auto& theme = lofibox::ui::defaultTheme();
    lofibox::ui::drawListPageFrame(canvas, theme);
    lofibox::ui::drawTopBar(canvas, theme, "NOW PLAYING", true);

    lofibox::core::Canvas no_back_canvas{lofibox::core::kDisplayWidth, lofibox::core::kDisplayHeight};
    lofibox::ui::drawListPageFrame(no_back_canvas, theme);
    lofibox::ui::drawTopBar(no_back_canvas, theme, "NOW PLAYING", false);
    for (int y = 4; y < 16; ++y) {
        for (int x = 4; x < 58; ++x) {
            if (canvas.pixel(x, y) != no_back_canvas.pixel(x, y)) {
                std::cerr << "Expected standard topbar left corner to stay F1:HELP even on back-capable pages.\n";
                return 1;
            }
        }
    }

    lofibox::ui::pages::renderNowPlayingPage(
        canvas,
        lofibox::ui::pages::NowPlayingView{
            true,
            "Spectrum Song",
            "Artist",
            "Album",
            180,
            37.0,
            lofibox::ui::pages::NowPlayingStatus::Playing,
            false,
            false,
            false,
            nullptr,
            "OFF",
            lofibox::ui::SpectrumFrame{
                true,
                std::array<float, 10>{0.12f, 0.22f, 0.46f, 0.80f, 1.0f, 0.62f, 0.34f, 0.20f, 0.52f, 0.72f}}}, theme);

    int bottom_spectrum_pixels = 0;
    int cyan_pixels = 0;
    int warm_pixels = 0;
    for (int y = 140; y <= 163; ++y) {
        for (int x = 12; x <= 308; ++x) {
            const auto pixel = canvas.pixel(x, y);
            if (!isBrightSpectrumPixel(pixel)) {
                continue;
            }
            ++bottom_spectrum_pixels;
            if (pixel.b > pixel.r && pixel.g > 70) {
                ++cyan_pixels;
            }
            if (pixel.r > pixel.b && pixel.g > 55) {
                ++warm_pixels;
            }
        }
    }

    if (bottom_spectrum_pixels < 120) {
        std::cerr << "Expected Now Playing bottom area to contain tidal spectrum pixels.\n";
        return 1;
    }

    if (cyan_pixels < 24 || warm_pixels < 24) {
        std::cerr << "Expected Now Playing spectrum to contain pitch-colored gradient pixels.\n";
        return 1;
    }

    int leaked_pixels = 0;
    for (int y = 138; y < 145; ++y) {
        for (int x = 12; x <= 308; ++x) {
            const auto pixel = canvas.pixel(x, y);
            if (pixel.g > 120 && pixel.b > 130) {
                ++leaked_pixels;
            }
        }
    }

    if (leaked_pixels > 32) {
        std::cerr << "Expected Now Playing spectrum to stay below the existing control hint area.\n";
        return 1;
    }

    return 0;
}
