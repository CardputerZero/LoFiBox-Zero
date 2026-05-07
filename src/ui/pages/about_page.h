// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <string>

#include "core/canvas.h"
#include "ui/ui_theme.h"

namespace lofibox::ui::pages {

struct AboutPageView {
    std::string version{};
    std::string storage{};
    std::string copyright{};
    std::string github_url{};
};

void renderAboutPage(core::Canvas& canvas, const AboutPageView& view, const UiTheme& theme);

} // namespace lofibox::ui::pages
