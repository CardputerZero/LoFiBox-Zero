// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <filesystem>
#include <string>

#include "ui/ui_theme.h"

namespace lofibox::plugins {

class SkinPluginAdapter {
public:
    bool loadSkinFromDir(const std::filesystem::path& plugin_dir);
    bool loadSkinFromJson(const std::filesystem::path& skin_json_path);

    [[nodiscard]] const ui::UiTheme& theme() const noexcept { return theme_; }

private:
    bool parsePaletteColor(const std::string& hex, core::Color& out) const;

    ui::UiTheme theme_{};
};

} // namespace lofibox::plugins
