// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <string>
#include <string_view>

#include "core/color.h"

namespace lofibox::ui {

struct UiPalette {
    core::Color background{core::rgba(5, 6, 8)};
    core::Color panel0{core::rgba(10, 12, 15)};
    core::Color panel1{core::rgba(16, 19, 24)};
    core::Color panel2{core::rgba(23, 27, 33)};
    core::Color chrome_topbar0{core::rgba(43, 46, 51)};
    core::Color chrome_topbar1{core::rgba(16, 19, 23)};
    core::Color divider{core::rgba(42, 47, 54)};
    core::Color text_primary{core::rgba(245, 247, 250)};
    core::Color text_secondary{core::rgba(199, 205, 211)};
    core::Color text_muted{core::rgba(141, 148, 156)};
    core::Color focus_fill{core::rgba(47, 134, 255)};
    core::Color focus_edge{core::rgba(169, 219, 255)};
    core::Color progress{core::rgba(88, 168, 255)};
    core::Color progress_strong{core::rgba(47, 117, 255)};
    core::Color good{core::rgba(135, 217, 108)};
    core::Color warn{core::rgba(230, 179, 74)};
    core::Color bad{core::rgba(214, 106, 95)};
};

struct UiSpacing {
    int top_bar_height{20};
    int list_top{28};
    int list_row_height{21};
    int max_visible_rows{6};
    int list_inset{8};
};

struct UiTheme {
    std::string id{"builtin-classic-dark"};
    UiPalette palette{};
    UiSpacing spacing{};
};

inline constexpr std::string_view kUnknown = "UNKNOWN";

inline const UiTheme& defaultTheme() noexcept
{
    static const UiTheme s_default{};
    return s_default;
}

} // namespace lofibox::ui
