// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui/pages/about_page.h"

#include "ui/ui_primitives.h"

namespace lofibox::ui::pages {

void renderAboutPage(core::Canvas& canvas, const AboutPageView& view, const UiTheme& theme)
{
    ::lofibox::ui::drawListPageFrame(canvas, theme);
    ::lofibox::ui::drawTopBar(canvas, theme, "ABOUT", true);
    canvas.fillRect(8, 30, 304, 126, theme.palette.panel1);
    canvas.strokeRect(8, 30, 304, 126, theme.palette.divider, 1);
    ::lofibox::ui::drawText(canvas, "LOFIBOX", 18, 42, theme.palette.text_primary, 1);
    ::lofibox::ui::drawText(canvas, "ZERO", 18, 58, theme.palette.text_primary, 2);
    ::lofibox::ui::drawText(canvas, "VERSION", 120, 40, theme.palette.text_muted, 1);
    ::lofibox::ui::drawText(canvas, view.version, 190, 40, theme.palette.text_primary, 1);
    ::lofibox::ui::drawText(canvas, "STORAGE", 120, 58, theme.palette.text_muted, 1);
    ::lofibox::ui::drawText(canvas, view.storage, 190, 58, theme.palette.text_primary, 1);
    ::lofibox::ui::drawText(canvas, "MODEL", 120, 76, theme.palette.text_muted, 1);
    ::lofibox::ui::drawText(canvas, "CARDPUTER ZERO", 176, 76, theme.palette.text_primary, 1);
    ::lofibox::ui::drawText(canvas, view.copyright, 18, 100, theme.palette.text_muted, 1);
    ::lofibox::ui::drawText(canvas, view.github_url, 18, 114, theme.palette.text_muted, 1);
    ::lofibox::ui::drawLine(canvas, 18, 128, 302, 128, theme.palette.divider);
    ::lofibox::ui::drawText(canvas, "Built with C++20  No external DSP libs", 18, 134, theme.palette.text_muted, 1);
    ::lofibox::ui::drawText(canvas, "All remix effects run in realtime on-device", 18, 146, theme.palette.text_muted, 1);
}

} // namespace lofibox::ui::pages
