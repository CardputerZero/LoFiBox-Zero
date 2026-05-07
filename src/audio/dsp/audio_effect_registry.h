// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace lofibox::audio::dsp {

struct AudioEffectDescriptor {
    std::string plugin_id{};
    std::string effect_id{};
    std::string name{};
    std::string description{};
    double default_intensity{1.0};
    bool builtin{true};
};

[[nodiscard]] std::string_view remixPluginId() noexcept;
[[nodiscard]] std::string_view remixRadioEffectId() noexcept;
[[nodiscard]] std::string_view remixTapeEffectId() noexcept;
[[nodiscard]] std::string_view remixVinylEffectId() noexcept;

[[nodiscard]] const std::vector<AudioEffectDescriptor>& builtinAudioEffects() noexcept;
[[nodiscard]] std::optional<AudioEffectDescriptor> audioEffectById(std::string_view effect_id);
[[nodiscard]] std::vector<AudioEffectDescriptor> audioEffectsForPlugin(std::string_view plugin_id);
[[nodiscard]] std::string cycleAudioEffectId(std::string_view plugin_id, std::string_view current_effect_id, int delta);
[[nodiscard]] std::string audioEffectName(std::string_view effect_id);
[[nodiscard]] bool audioEffectEnabled(std::string_view effect_id) noexcept;

} // namespace lofibox::audio::dsp
