// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <array>
#include <string>

#include "core/canvas.h"
#include "ui/ui_theme.h"

namespace lofibox::ui::pages {

struct EqualizerPageView {
    std::array<int, 10> bands{};
    int selected_band{0};
    std::string preset_name{};
};

void renderEqualizerPage(core::Canvas& canvas, const EqualizerPageView& view, const UiTheme& theme);

} // namespace lofibox::ui::pages
