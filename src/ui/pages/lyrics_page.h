// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <string>

#include "ui/pages/now_playing_page.h"
#include "core/canvas.h"
#include "ui/ui_models.h"
#include "ui/ui_theme.h"

namespace lofibox::ui::pages {

struct LyricsPageView {
    bool has_track{false};
    std::string title{};
    std::string artist{};
    int duration_seconds{0};
    double elapsed_seconds{0.0};
    NowPlayingStatus status{NowPlayingStatus::Empty};
    bool lookup_pending{false};
    LyricsContent lyrics{};
    SpectrumFrame visualization{};
};

void renderLyricsPage(core::Canvas& canvas, const LyricsPageView& view, const UiTheme& theme);

} // namespace lofibox::ui::pages
