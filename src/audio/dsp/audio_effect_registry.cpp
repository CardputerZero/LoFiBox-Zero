// SPDX-License-Identifier: GPL-3.0-or-later

#include "audio/dsp/audio_effect_registry.h"

#include <algorithm>

namespace lofibox::audio::dsp {
namespace {

constexpr std::string_view kRemixPluginId = "io.github.vicliu624.lofibox.effect.remix";
constexpr std::string_view kRemixRadioEffectId = "remix.radio";
constexpr std::string_view kRemixTapeEffectId = "remix.tape";
constexpr std::string_view kRemixVinylEffectId = "remix.vinyl";

const std::vector<AudioEffectDescriptor>& effects()
{
    static const std::vector<AudioEffectDescriptor> kEffects{
        {
            std::string{kRemixPluginId},
            std::string{kRemixRadioEffectId},
            "Radio",
            "Narrow-band broadcast tone with AM flutter, midrange focus, and light receiver hiss.",
            0.85,
            true,
        },
        {
            std::string{kRemixPluginId},
            std::string{kRemixTapeEffectId},
            "Tape",
            "Warm worn cassette color with softened highs, wow/flutter, saturation, and tape bed hiss.",
            1.0,
            true,
        },
        {
            std::string{kRemixPluginId},
            std::string{kRemixVinylEffectId},
            "Vinyl",
            "Turntable color with gentle rumble filtering, surface noise, dust ticks, and rounded transients.",
            1.0,
            true,
        },
    };
    return kEffects;
}

} // namespace

std::string_view remixPluginId() noexcept
{
    return kRemixPluginId;
}

std::string_view remixRadioEffectId() noexcept
{
    return kRemixRadioEffectId;
}

std::string_view remixTapeEffectId() noexcept
{
    return kRemixTapeEffectId;
}

std::string_view remixVinylEffectId() noexcept
{
    return kRemixVinylEffectId;
}

const std::vector<AudioEffectDescriptor>& builtinAudioEffects() noexcept
{
    return effects();
}

std::optional<AudioEffectDescriptor> audioEffectById(std::string_view effect_id)
{
    const auto found = std::find_if(effects().begin(), effects().end(), [effect_id](const AudioEffectDescriptor& effect) {
        return effect.effect_id == effect_id;
    });
    if (found == effects().end()) {
        return std::nullopt;
    }
    return *found;
}

std::vector<AudioEffectDescriptor> audioEffectsForPlugin(std::string_view plugin_id)
{
    std::vector<AudioEffectDescriptor> result{};
    for (const auto& effect : effects()) {
        if (plugin_id.empty() || effect.plugin_id == plugin_id) {
            result.push_back(effect);
        }
    }
    return result;
}

std::string cycleAudioEffectId(std::string_view plugin_id, std::string_view current_effect_id, int delta)
{
    auto group = audioEffectsForPlugin(plugin_id.empty() ? kRemixPluginId : plugin_id);
    if (group.empty()) {
        return {};
    }

    int current = 0; // 0 is OFF, 1..N are effects from the plugin.
    for (int index = 0; index < static_cast<int>(group.size()); ++index) {
        if (group[static_cast<std::size_t>(index)].effect_id == current_effect_id) {
            current = index + 1;
            break;
        }
    }

    const int count = static_cast<int>(group.size()) + 1;
    const int step = delta == 0 ? 1 : delta;
    const int next = ((current + step) % count + count) % count;
    if (next == 0) {
        return {};
    }
    return group[static_cast<std::size_t>(next - 1)].effect_id;
}

std::string audioEffectName(std::string_view effect_id)
{
    if (effect_id.empty()) {
        return "OFF";
    }
    if (const auto effect = audioEffectById(effect_id)) {
        return effect->name;
    }
    return std::string{effect_id};
}

bool audioEffectEnabled(std::string_view effect_id) noexcept
{
    return !effect_id.empty();
}

} // namespace lofibox::audio::dsp
