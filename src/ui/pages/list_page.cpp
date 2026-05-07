// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui/pages/list_page.h"

#include <algorithm>

#include "ui/ui_primitives.h"
#include "core/display_profile.h"

namespace lofibox::ui::pages {
namespace {

void drawListRowAt(
    core::Canvas& canvas,
    const UiTheme& theme,
    int top,
    int row_index,
    std::string_view primary,
    std::string_view secondary,
    bool selected)
{
    const int y = top + (row_index * theme.spacing.list_row_height);
    const auto primary_color = theme.palette.text_primary;
    const auto secondary_color = selected ? theme.palette.text_primary : theme.palette.text_secondary;

    canvas.fillRect(theme.spacing.list_inset, y, core::kDisplayWidth - (theme.spacing.list_inset * 2), theme.spacing.list_row_height - 1, theme.palette.panel1);
    if (selected) {
        ::lofibox::ui::drawGlassListFocus(
            canvas,
            theme,
            theme.spacing.list_inset,
            y,
            core::kDisplayWidth - (theme.spacing.list_inset * 2),
            theme.spacing.list_row_height - 1);
    } else {
        canvas.fillRect(theme.spacing.list_inset, y + theme.spacing.list_row_height - 2, core::kDisplayWidth - (theme.spacing.list_inset * 2), 1, theme.palette.divider);
    }

    ::lofibox::ui::drawText(canvas, ::lofibox::ui::fitUpper(primary, 22), theme.spacing.list_inset + 6, y + 6, primary_color, 1);
    if (!secondary.empty()) {
        ::lofibox::ui::drawText(canvas, ::lofibox::ui::fitUpper(secondary, 10), core::kDisplayWidth - 86, y + 6, secondary_color, 1);
    }
}

void drawEmptyRow(core::Canvas& canvas, const UiTheme& theme, std::string_view label)
{
    const int y = theme.spacing.list_top;
    canvas.fillRect(theme.spacing.list_inset, y, core::kDisplayWidth - (theme.spacing.list_inset * 2), theme.spacing.list_row_height - 1, theme.palette.panel1);
    canvas.fillRect(theme.spacing.list_inset, y + theme.spacing.list_row_height - 2, core::kDisplayWidth - (theme.spacing.list_inset * 2), 1, theme.palette.divider);
    ::lofibox::ui::drawText(canvas, label, theme.spacing.list_inset + 6, y + 6, theme.palette.text_muted, 1);
}

void drawListPositionIndicator(core::Canvas& canvas, const UiTheme& theme, int selected, int scroll, int item_count)
{
    if (item_count <= theme.spacing.max_visible_rows) {
        return;
    }

    const std::string position = std::to_string(std::clamp(selected + 1, 1, item_count)) + "/" + std::to_string(item_count);
    ::lofibox::ui::drawText(canvas, position, core::kDisplayWidth - 48, 6, theme.palette.text_secondary, 1);

    constexpr int track_x = core::kDisplayWidth - 7;
    const int track_y = theme.spacing.list_top;
    const int track_h = (theme.spacing.list_row_height * theme.spacing.max_visible_rows) - 2;
    constexpr int thumb_min_h = 10;

    const int thumb_h = std::max(thumb_min_h, (track_h * theme.spacing.max_visible_rows) / item_count);
    const int max_scroll = std::max(1, item_count - theme.spacing.max_visible_rows);
    const int clamped_scroll = std::clamp(scroll, 0, max_scroll);
    const int thumb_y = track_y + ((track_h - thumb_h) * clamped_scroll / max_scroll);
    ::lofibox::ui::drawGlassScrollbar(canvas, theme, track_x - 2, track_y, 6, track_h, thumb_y, thumb_h);
}

void drawShortcutHelpModal(core::Canvas& canvas, const UiTheme& theme)
{
    constexpr int x = 28;
    constexpr int y = 30;
    constexpr int w = core::kDisplayWidth - (x * 2);
    constexpr int h = 112;

    for (int row = 0; row < core::kDisplayHeight; ++row) {
        for (int col = 0; col < core::kDisplayWidth; ++col) {
            const auto pixel = canvas.pixel(col, row);
            canvas.setPixel(col, row, ::lofibox::ui::mixColor(pixel, theme.palette.background, 0.48f));
        }
    }

    canvas.fillRect(x, y, w, h, theme.palette.panel1);
    canvas.strokeRect(x, y, w, h, theme.palette.focus_edge, 1);
    ::lofibox::ui::drawGlassListFocus(canvas, theme, x + 1, y + 1, w - 2, 18);
    ::lofibox::ui::drawText(canvas, "SHORTCUTS", ::lofibox::ui::centeredX("SHORTCUTS", 1), y + 7, theme.palette.text_primary, 1);

    const struct Row {
        std::string_view key;
        std::string_view action;
    } rows[] = {
        {"OK", "OPEN / PLAY"},
        {"PGUP", "PAGE UP"},
        {"PGDN", "PAGE DOWN"},
        {"BACKSPACE", "BACK"},
        {"F2-F8", "PLAYBACK"},
        {"F9", "SEARCH"},
    };

    int text_y = y + 30;
    for (const auto& row : rows) {
        ::lofibox::ui::drawText(canvas, row.key, x + 14, text_y, theme.palette.progress, 1);
        ::lofibox::ui::drawText(canvas, row.action, x + 92, text_y, theme.palette.text_primary, 1);
        text_y += 14;
    }

}

} // namespace

void renderListPage(core::Canvas& canvas, const ListPageView& view, const UiTheme& theme)
{
    ::lofibox::ui::drawListPageFrame(canvas, theme);
    ::lofibox::ui::drawTopBar(canvas, theme, view.title, view.show_back, view.left_hint);

    if (view.rows.empty()) {
        drawEmptyRow(canvas, theme, view.empty_label);
        if (view.help_open) {
            drawShortcutHelpModal(canvas, theme);
        }
        return;
    }

    const int item_count = static_cast<int>(view.rows.size());
    const int start = std::clamp(view.scroll, 0, std::max(0, item_count - theme.spacing.max_visible_rows));
    const int end = std::min(item_count, start + theme.spacing.max_visible_rows);
    for (int index = start; index < end; ++index) {
        drawListRowAt(
            canvas,
            theme,
            theme.spacing.list_top,
            index - start,
            view.rows[static_cast<std::size_t>(index)].first,
            view.rows[static_cast<std::size_t>(index)].second,
            index == view.selected);
    }
    drawListPositionIndicator(canvas, theme, view.selected, start, item_count);
    if (view.help_open) {
        drawShortcutHelpModal(canvas, theme);
    }
}

} // namespace lofibox::ui::pages
