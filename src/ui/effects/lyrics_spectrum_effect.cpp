// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui/effects/lyrics_spectrum_effect.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

#include "core/display_profile.h"
#include "ui/ui_primitives.h"

namespace lofibox::ui::effects {
namespace {

[[nodiscard]] core::Color alphaBlend(core::Color dst, core::Color src, std::uint8_t opacity) noexcept
{
    const float src_alpha = (static_cast<float>(src.a) / 255.0f) * (static_cast<float>(opacity) / 255.0f);
    const float dst_alpha = static_cast<float>(dst.a) / 255.0f;
    const float out_alpha = src_alpha + (dst_alpha * (1.0f - src_alpha));
    if (out_alpha <= 0.0f) {
        return core::rgba(0, 0, 0, 0);
    }
    const auto blend = [&](std::uint8_t dst_c, std::uint8_t src_c) -> std::uint8_t {
        const float out = ((static_cast<float>(src_c) * src_alpha) + (static_cast<float>(dst_c) * dst_alpha * (1.0f - src_alpha))) / out_alpha;
        return static_cast<std::uint8_t>(std::clamp(out, 0.0f, 255.0f));
    };
    return core::rgba(blend(dst.r, src.r), blend(dst.g, src.g), blend(dst.b, src.b), static_cast<std::uint8_t>(std::clamp(out_alpha * 255.0f, 0.0f, 255.0f)));
}

void blendPixel(core::Canvas& canvas, int x, int y, core::Color color, std::uint8_t opacity)
{
    canvas.setPixel(x, y, alphaBlend(canvas.pixel(x, y), color, opacity));
}

[[nodiscard]] core::Color sideFoamHue(float rise_position) noexcept
{
    constexpr auto crest = core::rgba(255, 188, 88);
    constexpr auto mid = core::rgba(178, 78, 255);
    constexpr auto base = core::rgba(56, 202, 255);
    if (rise_position < 0.58f) {
        return ::lofibox::ui::mixColor(base, mid, rise_position / 0.58f);
    }
    return ::lofibox::ui::mixColor(mid, crest, (rise_position - 0.58f) / 0.42f);
}

[[nodiscard]] float sideFoamEnergy(const SpectrumFrame& frame, float rise_position) noexcept
{
    const float band_position = std::clamp(rise_position, 0.0f, 1.0f);
    const float scaled = band_position * static_cast<float>(frame.bands.size() - 1);
    const int lower = std::clamp(static_cast<int>(std::floor(scaled)), 0, static_cast<int>(frame.bands.size()) - 1);
    const int upper = std::clamp(lower + 1, 0, static_cast<int>(frame.bands.size()) - 1);
    const float blend = scaled - static_cast<float>(lower);
    return std::clamp((frame.bands[static_cast<std::size_t>(lower)] * (1.0f - blend)) + (frame.bands[static_cast<std::size_t>(upper)] * blend), 0.0f, 1.0f);
}

[[nodiscard]] float foamWave(float y, double elapsed_seconds, float side_phase) noexcept
{
    const float time = static_cast<float>(elapsed_seconds);
    const float wave_a = std::sin((y * 0.135f) + (time * 2.20f) + side_phase);
    const float wave_b = std::sin((y * 0.047f) - (time * 1.18f) + (side_phase * 0.71f));
    const float wave_c = std::sin((y * 0.021f) + (time * 0.66f) + (side_phase * 1.43f));
    return (wave_a * 0.48f) + (wave_b * 0.34f) + (wave_c * 0.18f);
}

[[nodiscard]] float adjacentEnergy(const SpectrumFrame& frame, float rise_position) noexcept
{
    constexpr float spread = 0.085f;
    const float lower = sideFoamEnergy(frame, std::clamp(rise_position - spread, 0.0f, 1.0f));
    const float center = sideFoamEnergy(frame, rise_position);
    const float upper = sideFoamEnergy(frame, std::clamp(rise_position + spread, 0.0f, 1.0f));
    return (lower * 0.24f) + (center * 0.52f) + (upper * 0.24f);
}

void renderFoamSide(core::Canvas& canvas, const SpectrumFrame& frame, double elapsed_seconds, bool left_side)
{
    constexpr int top = 34;
    constexpr int bottom = 148;
    constexpr int edge_margin = 4;
    constexpr int min_width = 4;
    constexpr int max_width = 42;
    const int edge_x = left_side ? edge_margin : (core::kDisplayWidth - edge_margin - 1);
    const int direction = left_side ? 1 : -1;
    const float side_phase = left_side ? 0.0f : 7.3f;
    for (int y = top; y <= bottom; ++y) {
        const float rise = static_cast<float>(bottom - y) / static_cast<float>(bottom - top);
        const float raw_energy = frame.available ? adjacentEnergy(frame, rise) : 0.0f;
        const float shaped_energy = static_cast<float>(std::pow(std::clamp(raw_energy * 1.45f, 0.0f, 1.0f), 0.46f));
        const float vertical_fade = 0.30f + (static_cast<float>(std::pow(1.0f - rise, 0.52f)) * 0.70f);
        const float slow_swell = 0.70f + (0.30f * static_cast<float>(std::sin((elapsed_seconds * 0.72) + (rise * 5.4f) + side_phase)));
        const float wave = foamWave(static_cast<float>(y), elapsed_seconds, side_phase);
        const float crest = std::max(0.0f, foamWave(static_cast<float>(y) + 9.0f, elapsed_seconds, side_phase + 2.2f));
        const float width_float = static_cast<float>(min_width) + (shaped_energy * slow_swell * static_cast<float>(max_width - min_width)) + (wave * (2.2f + shaped_energy * 5.4f));
        const int reach = std::clamp(static_cast<int>(std::round(width_float)), min_width, max_width);
        const auto color = sideFoamHue(rise);
        for (int offset = 0; offset <= reach; ++offset) {
            const float inward = static_cast<float>(offset) / static_cast<float>(std::max(1, reach));
            const float body = std::pow(1.0f - inward, 1.78f);
            const float boundary = std::exp(-std::pow((inward - 0.82f) * 4.2f, 2.0f));
            const float spray = std::max(0.0f, static_cast<float>(std::sin((static_cast<float>(y) * 0.31f) + (offset * 0.73f) + (elapsed_seconds * 2.4) + side_phase)));
            const float opacity_float = shaped_energy * vertical_fade * ((body * 82.0f) + (boundary * crest * 56.0f) + (spray * boundary * 18.0f));
            const auto opacity = static_cast<std::uint8_t>(std::clamp(opacity_float, 0.0f, 132.0f));
            if (opacity == 0) {
                continue;
            }
            const int x = edge_x + (direction * offset);
            blendPixel(canvas, x, y, color, opacity);
            if ((offset % 3) == 0 && y + 1 <= bottom) {
                blendPixel(canvas, x, y + 1, color, static_cast<std::uint8_t>(opacity * 0.42f));
            }
        }
    }
}

} // namespace

void renderLyricsSideFoamSpectrum(core::Canvas& canvas, const SpectrumFrame& frame, double elapsed_seconds)
{
    renderFoamSide(canvas, frame, elapsed_seconds, true);
    renderFoamSide(canvas, frame, elapsed_seconds, false);
}

} // namespace lofibox::ui::effects
