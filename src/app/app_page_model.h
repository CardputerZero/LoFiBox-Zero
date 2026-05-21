// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "app/app_page.h"
#include "app/app_state.h"

namespace lofibox::app {

inline constexpr int kPageModelMaxVisibleRows = 6;

struct AppPageModelInput {
    AppPage page{AppPage::Boot};
    std::optional<std::string> library_title_override{};
    std::optional<std::vector<std::pair<std::string, std::string>>> library_rows{};
    SettingsState settings{};
    bool network_connected{false};
    std::string metadata_display_name{};
    std::string webui_url{};
    std::optional<std::vector<std::pair<std::string, std::string>>> source_manager_rows{};
    std::optional<std::vector<std::pair<std::string, std::string>>> page_rows{};
};

struct AppPageModel {
    std::string title{};
    std::vector<std::pair<std::string, std::string>> rows{};
    bool browse_list{false};
    int max_visible_rows{kPageModelMaxVisibleRows};
};

[[nodiscard]] AppPageModel buildAppPageModel(const AppPageModelInput& input);
[[nodiscard]] bool isBrowseListPage(AppPage page) noexcept;

} // namespace lofibox::app
