// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui/pages/lyrics_page.h"

#include <algorithm>
#include <cstdlib>

#include "core/display_profile.h"
#include "ui/ui_primitives.h"
#include "ui/effects/lyrics_spectrum_effect.h"
#include "ui/widgets/lyrics_layout.h"

namespace lofibox::ui::pages {

void renderLyricsPage(core::Canvas& canvas, const LyricsPageView& view, const UiTheme& theme)
{
    canvas.fillRect(0, theme.spacing.top_bar_height, core::kDisplayWidth, core::kDisplayHeight - theme.spacing.top_bar_height, theme.palette.background);
    canvas.fillRect(6, 27, 308, 135, theme.palette.panel0);
    canvas.fillRect(6, 27, 308, 1, theme.palette.divider);
    ::lofibox::ui::effects::renderLyricsSideFoamSpectrum(canvas, view.visualization, view.elapsed_seconds);

    if (!view.has_track) {
        ::lofibox::ui::drawText(canvas, "NO TRACK", ::lofibox::ui::centeredX("NO TRACK", 1), 70, theme.palette.text_primary, 1);
        ::lofibox::ui::drawText(canvas, "SELECT MUSIC TO PLAY", ::lofibox::ui::centeredX("SELECT MUSIC TO PLAY", 1), 92, theme.palette.text_muted, 1);
        return;
    }

    ::lofibox::ui::drawText(canvas, ::lofibox::ui::fitText(view.title, 30), ::lofibox::ui::centeredX(::lofibox::ui::fitText(view.title, 30), 1), 32, theme.palette.text_primary, 1);
    ::lofibox::ui::drawText(canvas, ::lofibox::ui::fitText(view.artist, 28), ::lofibox::ui::centeredX(::lofibox::ui::fitText(view.artist, 28), 1), 46, theme.palette.text_muted, 1);

    const auto lines = ::lofibox::ui::widgets::lyricDisplayLines(view.lyrics);
    if (lines.empty()) {
        const std::string primary = view.lookup_pending ? "SEARCHING LYRICS" : "LYRICS NOT FOUND";
        const std::string secondary = view.lookup_pending ? "ONLINE MATCH IN PROGRESS" : "NO MATCH FROM TAGS OR ONLINE";
        canvas.fillRect(54, 72, 212, 1, theme.palette.divider);
        canvas.fillRect(72, 92, 176, 1, theme.palette.divider);
        for (int index = 0; index < 5; ++index) {
            const int x = 122 + (index * 18);
            const int h = 6 + ((index % 3) * 5);
            const auto color = view.lookup_pending && index == static_cast<int>(static_cast<long long>(view.elapsed_seconds) % 5)
                ? theme.palette.progress_strong
                : theme.palette.panel2;
            canvas.fillRect(x, 80 - h, 8, h, color);
        }
        ::lofibox::ui::drawText(canvas, primary, ::lofibox::ui::centeredX(primary, 1), 96, view.lookup_pending ? theme.palette.text_primary : theme.palette.text_secondary, 1);
        ::lofibox::ui::drawText(canvas, secondary, ::lofibox::ui::centeredX(secondary, 1), 114, theme.palette.text_muted, 1);
        ::lofibox::ui::drawText(canvas, "L: BACK TO PLAYER", ::lofibox::ui::centeredX("L: BACK TO PLAYER", 1), 136, theme.palette.text_muted, 1);
    } else {
        constexpr int visible_lines = 7;
        constexpr int row_h = 14;
        constexpr std::size_t active_line_chars = 44;
        constexpr std::size_t inactive_line_chars = 40;
        const int active = ::lofibox::ui::widgets::activeLyricIndex(lines, view.elapsed_seconds, view.duration_seconds);
        const int first = active - (visible_lines / 2);
        for (int row = 0; row < visible_lines; ++row) {
            const int index = first + row;
            if (index < 0 || index >= static_cast<int>(lines.size())) {
                continue;
            }
            const int y = 62 + (row * row_h);
            const bool is_active = index == active;
            const std::string text = ::lofibox::ui::fitText(
                lines[static_cast<std::size_t>(index)].text,
                is_active ? active_line_chars : inactive_line_chars);
            const int x = ::lofibox::ui::centeredX(text, 1);
            const auto color = is_active ? theme.palette.text_primary : (std::abs(index - active) <= 1 ? theme.palette.text_secondary : theme.palette.text_muted);
            if (is_active) {
                ::lofibox::ui::drawSoftLyricFocus(canvas, theme, 34, y + 6, 252, 30);
                ::lofibox::ui::drawText(canvas, text, x, y + 2, color, 1);
            } else {
                ::lofibox::ui::drawText(canvas, text, x, y + 2, color, 1);
            }
        }

    }

    ::lofibox::ui::drawFloatingProgressBar(
        canvas,
        theme,
        55,
        154,
        210,
        std::max(0, std::min(210, static_cast<int>((view.elapsed_seconds / std::max(1, view.duration_seconds)) * 210.0))));
}

} // namespace lofibox::ui::pages
