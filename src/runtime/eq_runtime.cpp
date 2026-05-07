// SPDX-License-Identifier: GPL-3.0-or-later

#include "runtime/eq_runtime.h"

#include <algorithm>
#include <cmath>
#include <cctype>
#include <string>
#include <utility>

#include "audio/dsp/audio_effect_registry.h"
#include "audio/dsp/dsp_chain.h"

namespace lofibox::runtime {
namespace {

constexpr int kEqMinGainDb = -12;
constexpr int kEqMaxGainDb = 12;

std::string upperAscii(std::string_view text)
{
    std::string result{text};
    std::transform(result.begin(), result.end(), result.begin(), [](unsigned char ch) {
        return static_cast<char>(std::toupper(ch));
    });
    return result;
}

bool hasNonZeroGain(const EqRuntimeState& state) noexcept
{
    return std::any_of(state.bands.begin(), state.bands.end(), [](int gain) {
        return gain != 0;
    });
}

audio::dsp::DspChainProfile dspProfileFromEqState(const EqRuntimeState& state)
{
    auto eq = audio::dsp::tenBandFlatEqProfile();
    eq.id = "runtime-current";
    eq.name = state.preset_name.empty() ? std::string{"CUSTOM"} : state.preset_name;

    bool has_gain = false;
    for (std::size_t index = 0; index < eq.bands.size() && index < state.bands.size(); ++index) {
        eq.bands[index].gain_db = static_cast<double>(state.bands[index]);
        has_gain = has_gain || state.bands[index] != 0;
    }
    eq.enabled = state.enabled && has_gain;
    eq.limiter_enabled = true;
    eq.limiter_ceiling_db = -1.0;

    audio::dsp::DspChainProfile profile{};
    profile.active_profile_id = eq.id;
    profile.eq = std::move(eq);
    profile.limiter.enabled = true;
    profile.limiter.ceiling_db = -1.0;
    profile.effect.plugin_id = state.effect_plugin_id;
    profile.effect.effect_id = state.effect_id;
    profile.effect.name = audio::dsp::audioEffectName(state.effect_id);
    profile.effect.intensity = state.effect_intensity;
    return profile;
}

} // namespace

EqRuntime::EqRuntime(application::AppServiceRegistry services, EqRuntimeState& eq) noexcept
    : services_(services),
      eq_(eq)
{
}

void EqRuntime::setEnabled(bool enabled)
{
    eq_.enabled = enabled;
    applyToPlayback();
}

bool EqRuntime::setBand(int band_index, int gain_db)
{
    if (band_index < 0 || band_index >= static_cast<int>(eq_.bands.size())) {
        return false;
    }
    eq_.bands[static_cast<std::size_t>(band_index)] = std::clamp(gain_db, kEqMinGainDb, kEqMaxGainDb);
    eq_.preset_name = "CUSTOM";
    eq_.enabled = hasNonZeroGain(eq_);
    applyToPlayback();
    return true;
}

bool EqRuntime::adjustBand(int band_index, int delta_db)
{
    if (band_index < 0 || band_index >= static_cast<int>(eq_.bands.size())) {
        return false;
    }
    const auto current = eq_.bands[static_cast<std::size_t>(band_index)];
    return setBand(band_index, current + delta_db);
}

bool EqRuntime::applyPreset(std::string_view preset_name)
{
    const auto wanted = upperAscii(preset_name);
    for (const auto& preset : audio::dsp::builtinEqPresets()) {
        if (upperAscii(preset.name) != wanted) {
            continue;
        }
        for (std::size_t index = 0; index < eq_.bands.size() && index < preset.bands.size(); ++index) {
            eq_.bands[index] = std::clamp(static_cast<int>(std::round(preset.bands[index].gain_db)), kEqMinGainDb, kEqMaxGainDb);
        }
        eq_.preset_name = upperAscii(preset.name);
        eq_.enabled = hasNonZeroGain(eq_);
        applyToPlayback();
        return true;
    }
    return false;
}

bool EqRuntime::cyclePreset(int delta)
{
    auto presets = audio::dsp::builtinEqPresets();
    if (presets.empty()) {
        return false;
    }

    int current = -1;
    const auto current_name = upperAscii(eq_.preset_name);
    for (int index = 0; index < static_cast<int>(presets.size()); ++index) {
        if (upperAscii(presets[static_cast<std::size_t>(index)].name) == current_name) {
            current = index;
            break;
        }
    }

    const int count = static_cast<int>(presets.size());
    const int next = ((current + delta) % count + count) % count;
    return applyPreset(presets[static_cast<std::size_t>(next)].name);
}

bool EqRuntime::cycleAudioEffect(std::string_view plugin_id, int delta)
{
    const auto resolved_plugin_id = plugin_id.empty() ? audio::dsp::remixPluginId() : plugin_id;
    if (audio::dsp::audioEffectsForPlugin(resolved_plugin_id).empty()) {
        return false;
    }
    const auto next = audio::dsp::cycleAudioEffectId(resolved_plugin_id, eq_.effect_id, delta);
    if (next.empty()) {
        eq_.effect_plugin_id.clear();
        eq_.effect_id.clear();
    } else if (const auto effect = audio::dsp::audioEffectById(next)) {
        eq_.effect_plugin_id = effect->plugin_id;
        eq_.effect_id = effect->effect_id;
        eq_.effect_intensity = effect->default_intensity;
    } else {
        return false;
    }
    applyToPlayback();
    return true;
}

void EqRuntime::reset()
{
    eq_.bands.fill(0);
    eq_.preset_name = "FLAT";
    eq_.enabled = false;
    applyToPlayback();
}

EqRuntimeSnapshot EqRuntime::snapshot(std::uint64_t version) const
{
    EqRuntimeSnapshot result{};
    result.bands = eq_.bands;
    result.preset_name = eq_.preset_name;
    result.enabled = eq_.enabled;
    result.effect_plugin_id = eq_.effect_plugin_id;
    result.effect_id = eq_.effect_id;
    result.effect_name = audio::dsp::audioEffectName(eq_.effect_id);
    result.effect_intensity = eq_.effect_intensity;
    result.effect_enabled = audio::dsp::audioEffectEnabled(eq_.effect_id);
    result.version = version;
    return result;
}

void EqRuntime::applyToPlayback() const
{
    services_.playbackCommands().setDspProfile(dspProfileFromEqState(eq_));
}

} // namespace lofibox::runtime
