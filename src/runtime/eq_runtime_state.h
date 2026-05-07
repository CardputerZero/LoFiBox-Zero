// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <array>
#include <string>

namespace lofibox::runtime {

struct EqRuntimeState {
    std::array<int, 10> bands{0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    std::string preset_name{"FLAT"};
    bool enabled{false};
    std::string effect_plugin_id{};
    std::string effect_id{};
    double effect_intensity{1.0};
};

} // namespace lofibox::runtime
