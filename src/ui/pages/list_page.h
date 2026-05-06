// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <string>
#include <utility>
#include <vector>

#include "core/canvas.h"
#include "ui/ui_theme.h"

namespace lofibox::ui::pages {

struct ListPageView {
    std::string title{};
    bool show_back{true};
    std::string left_hint{};
    std::vector<std::pair<std::string, std::string>> rows{};
    std::string empty_label{};
    int selected{0};
    int scroll{0};
    bool help_open{false};
};

void renderListPage(core::Canvas& canvas, const ListPageView& view, const UiTheme& theme);

} // namespace lofibox::ui::pages
