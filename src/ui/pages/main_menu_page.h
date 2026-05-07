// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "core/canvas.h"
#include "ui/ui_models.h"
#include "ui/ui_theme.h"

namespace lofibox::ui::pages {

enum class MenuIndexState {
    Uninitialized,
    Loading,
    Ready,
    Degraded,
};

struct MenuStorageView {
    bool available{false};
    std::uintmax_t capacity_bytes{0};
    std::uintmax_t free_bytes{0};
};

struct MainMenuView {
    int selected{1};
    MenuStorageView storage{};
    MenuIndexState index_state{MenuIndexState::Uninitialized};
    bool network_connected{false};
    const UiAssets* assets{};
    std::string playback_summary{"NO TRACK"};
    int playback_summary_scroll_px{0};
};

void renderMainMenuPage(core::Canvas& canvas, const MainMenuView& view, const UiTheme& theme);

} // namespace lofibox::ui::pages
