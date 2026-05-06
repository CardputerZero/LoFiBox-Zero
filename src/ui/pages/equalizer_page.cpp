// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui/pages/equalizer_page.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <string>
#include <string_view>

#include "core/bitmap_font.h"
#include "ui/ui_primitives.h"

namespace lofibox::ui::pages {
namespace {

using core::rgba;

inline constexpr int kPanelX = 6;
inline constexpr int kPanelY = 24;
inline constexpr int kPanelW = 308;
inline constexpr int kPanelH = 140;
inline constexpr int kGraphX = 78;
inline constexpr int kGainY = 30;
inline constexpr int kSliderY = 44;
inline constexpr int kSliderW = 18;
inline constexpr int kSliderH = 54;
inline constexpr int kSliderGap = 4;
inline constexpr int kSliderStride = kSliderW + kSliderGap;
inline constexpr int kZeroY = kSliderY + (kSliderH / 2);

inline constexpr auto kEqHot0 = core::rgba(255, 178, 87);
inline constexpr auto kEqHot1 = core::rgba(255, 127, 42);
inline constexpr auto kEqHotEdge = core::rgba(255, 224, 176);
inline constexpr auto kEqSelected0 = core::rgba(127, 209, 255);
inline constexpr auto kEqSelected1 = core::rgba(47, 134, 255);
inline constexpr auto kTrackTop = core::rgba(36, 42, 52);
inline constexpr auto kTrackBottom = core::rgba(12, 15, 20);

[[nodiscard]] core::Color alphaBlend(core::Color dst, core::Color src, std::uint8_t opacity) noexcept
{
    const float src_alpha = (static_cast<float>(src.a) / 255.0f) * (static_cast<float>(opacity) / 255.0f);
    const float dst_alpha = static_cast<float>(dst.a) / 255.0f;
    const float out_alpha = src_alpha + (dst_alpha * (1.0f - src_alpha));
    if (out_alpha <= 0.0f) {
        return rgba(0, 0, 0, 0);
    }

    const auto blend = [&](std::uint8_t dst_c, std::uint8_t src_c) -> std::uint8_t {
        const float out =
            ((static_cast<float>(src_c) * src_alpha) + (static_cast<float>(dst_c) * dst_alpha * (1.0f - src_alpha))) / out_alpha;
        return static_cast<std::uint8_t>(std::clamp(out, 0.0f, 255.0f));
    };

    return rgba(
        blend(dst.r, src.r),
        blend(dst.g, src.g),
        blend(dst.b, src.b),
        static_cast<std::uint8_t>(std::clamp(out_alpha * 255.0f, 0.0f, 255.0f)));
}

void blendPixel(core::Canvas& canvas, int x, int y, core::Color color, std::uint8_t opacity)
{
    canvas.setPixel(x, y, alphaBlend(canvas.pixel(x, y), color, opacity));
}

void drawVerticalGlassFill(core::Canvas& canvas, int x, int y, int width, int height, core::Color top, core::Color bottom, std::uint8_t opacity)
{
    if (width <= 0 || height <= 0) {
        return;
    }

    for (int row = 0; row < height; ++row) {
        const float vertical = static_cast<float>(row) / static_cast<float>(std::max(1, height - 1));
        const auto color = ::lofibox::ui::mixColor(top, bottom, vertical);
        const float edge_y = static_cast<float>(std::min(row, height - 1 - row)) / 5.0f;
        const float vertical_fade = std::clamp(0.55f + (0.45f * edge_y), 0.55f, 1.0f);
        for (int col = 0; col < width; ++col) {
            const float edge_x = static_cast<float>(std::min(col, width - 1 - col)) / 4.0f;
            const float horizontal = std::clamp(0.62f + (0.38f * edge_x), 0.62f, 1.0f);
            blendPixel(canvas, x + col, y + row, color, static_cast<std::uint8_t>(static_cast<float>(opacity) * horizontal * vertical_fade));
        }
    }
}

void drawCenteredText(core::Canvas& canvas, std::string_view text, int center_x, int y, core::Color color)
{
    const int width = core::bitmap_font::measureText(text, 1);
    ::lofibox::ui::drawText(canvas, text, center_x - (width / 2), y, color, 1);
}

[[nodiscard]] std::string formatGain(int gain)
{
    return gain > 0 ? "+" + std::to_string(gain) : std::to_string(gain);
}

[[nodiscard]] std::string formatGainDb(int gain)
{
    return formatGain(gain) + "DB";
}

void drawPanelChrome(core::Canvas& canvas, const UiTheme& theme)
{
    canvas.fillRect(kPanelX, kPanelY, kPanelW, kPanelH, theme.palette.panel1);
    drawVerticalGlassFill(canvas, kPanelX + 1, kPanelY + 1, kPanelW - 2, kPanelH - 2, rgba(28, 34, 42), rgba(9, 11, 15), 92);
    canvas.fillRect(kPanelX + 8, kPanelY + 5, kPanelW - 16, 1, rgba(80, 94, 112));
    canvas.strokeRect(kPanelX, kPanelY, kPanelW, kPanelH, theme.palette.divider, 1);
}

void drawSliderTrack(core::Canvas& canvas, const UiTheme& theme, int x, bool selected)
{
    if (selected) {
        ::lofibox::ui::drawGlassListFocus(canvas, theme, x - 3, kSliderY - 3, kSliderW + 6, kSliderH + 6);
    }
    canvas.fillRoundedRect(x, kSliderY, kSliderW, kSliderH, 4, kTrackBottom);
    drawVerticalGlassFill(canvas, x + 1, kSliderY + 1, kSliderW - 2, kSliderH - 2, kTrackTop, kTrackBottom, 120);
    canvas.fillRect(x + 4, kSliderY + 2, kSliderW - 8, 1, rgba(118, 132, 152));
    canvas.strokeRect(x, kSliderY, kSliderW, kSliderH, selected ? kEqSelected0 : theme.palette.divider, 1);
}

void drawGainFill(core::Canvas& canvas, const UiTheme& theme, int x, int gain, bool selected)
{
    const int max_pixels = (kSliderH / 2) - 2;
    const int pixels = std::clamp(static_cast<int>(std::round((std::abs(gain) / 12.0f) * static_cast<float>(max_pixels))), 0, max_pixels);
    const int fill_x = x + 3;
    constexpr int kFillW = kSliderW - 6;
    int fill_y = kZeroY;
    int fill_h = 1;
    if (gain > 0) {
        fill_y = kZeroY - pixels;
        fill_h = pixels + 1;
    } else if (gain < 0) {
        fill_y = kZeroY;
        fill_h = pixels + 1;
    }

    const auto top = selected ? kEqSelected0 : kEqHot0;
    const auto bottom = selected ? kEqSelected1 : kEqHot1;
    canvas.fillRoundedRect(fill_x, fill_y, kFillW, fill_h, std::min(4, std::max(1, fill_h / 2)), bottom);
    drawVerticalGlassFill(canvas, fill_x, fill_y, kFillW, fill_h, top, bottom, 230);
    canvas.fillRect(fill_x + 2, fill_y, kFillW - 4, 1, selected ? theme.palette.focus_edge : kEqHotEdge);
    if (selected) {
        blendPixel(canvas, fill_x - 1, fill_y, kEqSelected0, 96);
        blendPixel(canvas, fill_x + kFillW, fill_y, kEqSelected0, 96);
    }
}

void drawZeroLine(core::Canvas& canvas, const UiTheme& theme)
{
    canvas.fillRect(kGraphX - 3, kZeroY, 226, 1, theme.palette.divider);
    for (int col = 0; col < 226; ++col) {
        const float edge = static_cast<float>(std::min(col, 225 - col)) / 26.0f;
        blendPixel(canvas, kGraphX - 3 + col, kZeroY, rgba(174, 198, 220), static_cast<std::uint8_t>(72.0f * std::clamp(edge, 0.0f, 1.0f)));
    }
}

} // namespace

void renderEqualizerPage(core::Canvas& canvas, const EqualizerPageView& view, const UiTheme& theme)
{
    ::lofibox::ui::drawListPageFrame(canvas, theme);
    ::lofibox::ui::drawTopBar(canvas, theme, "EQUALIZER", true);
    drawPanelChrome(canvas, theme);
    constexpr std::array<std::string_view, 10> labels{"31", "62", "125", "250", "500", "1K", "2K", "4K", "8K", "16K"};
    const int selected = std::clamp(view.selected_band, 0, static_cast<int>(labels.size()) - 1);

    ::lofibox::ui::drawText(canvas, "BAND", 12, 35, theme.palette.text_muted, 1);
    ::lofibox::ui::drawText(canvas, labels[static_cast<std::size_t>(selected)], 12, 48, theme.palette.text_primary, 1);
    ::lofibox::ui::drawText(canvas, "GAIN", 12, 83, theme.palette.text_muted, 1);
    ::lofibox::ui::drawText(canvas, formatGainDb(view.bands[static_cast<std::size_t>(selected)]), 12, 96, kEqHotEdge, 1);

    ::lofibox::ui::drawText(canvas, "+12", 50, 45, theme.palette.text_muted, 1);
    ::lofibox::ui::drawText(canvas, "0", 58, 67, theme.palette.text_secondary, 1);
    ::lofibox::ui::drawText(canvas, "-12", 46, 94, theme.palette.text_muted, 1);
    drawZeroLine(canvas, theme);

    for (int index = 0; index < 10; ++index) {
        const int x = kGraphX + (index * kSliderStride);
        const bool selected_band = index == selected;
        const int gain = view.bands[static_cast<std::size_t>(index)];
        drawCenteredText(canvas, formatGain(gain), x + (kSliderW / 2), kGainY, selected_band ? theme.palette.text_primary : theme.palette.text_muted);
        drawSliderTrack(canvas, theme, x, selected_band);
        drawGainFill(canvas, theme, x, gain, selected_band);
        canvas.fillRect(x + 2, kZeroY, kSliderW - 4, 1, selected_band ? theme.palette.focus_edge : rgba(118, 132, 152));
        drawCenteredText(canvas, labels[static_cast<std::size_t>(index)], x + (kSliderW / 2), 118, selected_band ? theme.palette.text_primary : theme.palette.text_muted);
    }

    canvas.fillRect(8, 142, 304, 16, theme.palette.panel2);
    drawVerticalGlassFill(canvas, 9, 143, 302, 14, rgba(34, 40, 48), rgba(12, 15, 20), 130);
    canvas.strokeRect(8, 142, 304, 16, theme.palette.divider, 1);
    ::lofibox::ui::drawText(canvas, "PRESET", 14, 145, theme.palette.text_muted, 1);
    const auto preset = "< " + ::lofibox::ui::fitUpper(view.preset_name, 14) + " >";
    drawCenteredText(canvas, preset, 174, 145, theme.palette.text_secondary);
}

} // namespace lofibox::ui::pages
