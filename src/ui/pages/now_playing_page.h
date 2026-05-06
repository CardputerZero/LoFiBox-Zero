// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <array>
#include <optional>
#include <string>

#include "core/canvas.h"
#include "ui/ui_models.h"
#include "ui/ui_theme.h"

namespace lofibox::ui::pages {

enum class NowPlayingStatus {
    Empty,
    Paused,
    Playing,
};

struct NowPlayingView {
    bool has_track{false};
    std::string title{};
    std::string artist{};
    std::string album{};
    int duration_seconds{0};
    double elapsed_seconds{0.0};
    NowPlayingStatus status{NowPlayingStatus::Empty};
    bool shuffle_enabled{false};
    bool repeat_all{false};
    bool repeat_one{false};
    const core::Canvas* artwork{};
    SpectrumFrame visualization{};
};

void renderNowPlayingPage(core::Canvas& canvas, const NowPlayingView& view, const UiTheme& theme);

} // namespace lofibox::ui::pages
