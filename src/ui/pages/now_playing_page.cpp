// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui/pages/now_playing_page.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdint>

#include "ui/ui_primitives.h"

namespace lofibox::ui::pages {
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
        const float out =
            ((static_cast<float>(src_c) * src_alpha) + (static_cast<float>(dst_c) * dst_alpha * (1.0f - src_alpha))) / out_alpha;
        return static_cast<std::uint8_t>(std::clamp(out, 0.0f, 255.0f));
    };

    return core::rgba(
        blend(dst.r, src.r),
        blend(dst.g, src.g),
        blend(dst.b, src.b),
        static_cast<std::uint8_t>(std::clamp(out_alpha * 255.0f, 0.0f, 255.0f)));
}

void blendPixel(core::Canvas& canvas, int x, int y, core::Color color, std::uint8_t opacity)
{
    canvas.setPixel(x, y, alphaBlend(canvas.pixel(x, y), color, opacity));
}

std::string formatDuration(int seconds)
{
    const int minutes = std::max(seconds, 0) / 60;
    const int remainder = std::max(seconds, 0) % 60;

    char buffer[16]{};
    std::snprintf(buffer, sizeof(buffer), "%02d:%02d", minutes, remainder);
    return buffer;
}

[[nodiscard]] core::Color spectrumHue(float band_position) noexcept
{
    constexpr auto low = core::rgba(56, 202, 255);
    constexpr auto mid = core::rgba(178, 78, 255);
    constexpr auto high = core::rgba(255, 177, 68);

    if (band_position < 0.55f) {
        return ::lofibox::ui::mixColor(low, mid, band_position / 0.55f);
    }
    return ::lofibox::ui::mixColor(mid, high, (band_position - 0.55f) / 0.45f);
}

[[nodiscard]] float spectrumEnergyForColumn(const std::array<float, 10>& bands, int column, int column_count) noexcept
{
    const float band_position = static_cast<float>(column) / static_cast<float>(std::max(1, column_count - 1));
    const float scaled = band_position * static_cast<float>(bands.size() - 1);
    const int lower = std::clamp(static_cast<int>(std::floor(scaled)), 0, static_cast<int>(bands.size()) - 1);
    const int upper = std::clamp(lower + 1, 0, static_cast<int>(bands.size()) - 1);
    const float blend = scaled - static_cast<float>(lower);
    return std::clamp(
        (bands[static_cast<std::size_t>(lower)] * (1.0f - blend)) +
            (bands[static_cast<std::size_t>(upper)] * blend),
        0.0f,
        1.0f);
}

void renderTidalSpectrum(core::Canvas& canvas, const NowPlayingView& view, const UiTheme& theme)
{
    constexpr int kLeft = 16;
    constexpr int kRight = 304;
    constexpr int kBaseline = 160;
    constexpr int kMaxHeight = 24;
    constexpr int kColumnWidth = 4;
    constexpr int kGap = 3;
    constexpr int kStride = kColumnWidth + kGap;
    constexpr int kColumnCount = (kRight - kLeft) / kStride;

    for (int column = 0; column < kColumnCount; ++column) {
        const float position = static_cast<float>(column) / static_cast<float>(std::max(1, kColumnCount - 1));
        const float energy = view.visualization.available ? spectrumEnergyForColumn(view.visualization.bands, column, kColumnCount) : 0.0f;
        const float visible_energy = std::pow(std::clamp(energy * 1.65f, 0.0f, 1.0f), 0.52f);
        const int height = view.visualization.available
            ? std::clamp(static_cast<int>(std::round(1.0f + (visible_energy * static_cast<float>(kMaxHeight - 1)))), 1, kMaxHeight)
            : 2;
        const int x = kLeft + (column * kStride);
        const auto base_color = spectrumHue(position);

        for (int row = 0; row < height; ++row) {
            const float vertical = static_cast<float>(row) / static_cast<float>(std::max(1, height - 1));
            const int y = kBaseline - row;
            const auto column_color = ::lofibox::ui::mixColor(base_color, theme.palette.text_primary, 0.34f * vertical);
            const std::uint8_t opacity = view.visualization.available
                ? static_cast<std::uint8_t>(std::clamp(42.0f + (118.0f * vertical), 0.0f, 160.0f))
                : static_cast<std::uint8_t>(36);
            for (int pixel_x = 0; pixel_x < kColumnWidth; ++pixel_x) {
                const float edge = static_cast<float>(std::min(pixel_x, kColumnWidth - 1 - pixel_x)) / 2.0f;
                const std::uint8_t faded_opacity = static_cast<std::uint8_t>(static_cast<float>(opacity) * std::clamp(0.58f + edge, 0.58f, 1.0f));
                blendPixel(canvas, x + pixel_x, y, column_color, faded_opacity);
            }
        }

        for (int glow = 1; glow <= 3; ++glow) {
            const int y = kBaseline + glow;
            const auto glow_color = ::lofibox::ui::mixColor(base_color, theme.palette.background, 0.35f);
            blendPixel(canvas, x + 1, y, glow_color, static_cast<std::uint8_t>(38 / glow));
            blendPixel(canvas, x + 2, y, glow_color, static_cast<std::uint8_t>(38 / glow));
        }
    }
}

} // namespace

void renderNowPlayingPage(core::Canvas& canvas, const NowPlayingView& view, const UiTheme& theme)
{
    if (!view.has_track) {
        ::lofibox::ui::drawText(canvas, "NO TRACK", 122, 38, theme.palette.text_primary, 1);
        ::lofibox::ui::drawText(canvas, "SELECT MUSIC TO PLAY", 122, 58, theme.palette.text_muted, 1);
        return;
    }

    constexpr int kArtworkFrameX = 32;
    constexpr int kArtworkFrameY = 46;
    constexpr int kArtworkFrameSize = 72;
    constexpr int kArtworkInset = 2;
    constexpr int kArtworkSize = kArtworkFrameSize - (kArtworkInset * 2);

    if (view.artwork != nullptr) {
        ::lofibox::ui::blitScaledCanvas(
            canvas,
            *view.artwork,
            kArtworkFrameX + kArtworkInset,
            kArtworkFrameY + kArtworkInset,
            kArtworkSize,
            kArtworkSize,
            255);
        canvas.strokeRect(kArtworkFrameX, kArtworkFrameY, kArtworkFrameSize, kArtworkFrameSize, theme.palette.divider, 1);
    } else {
        canvas.fillRect(kArtworkFrameX, kArtworkFrameY, kArtworkFrameSize, kArtworkFrameSize, theme.palette.panel1);
        canvas.strokeRect(kArtworkFrameX, kArtworkFrameY, kArtworkFrameSize, kArtworkFrameSize, theme.palette.divider, 1);
        canvas.fillRect(kArtworkFrameX + 18, kArtworkFrameY + 28, 16, 34, theme.palette.progress_strong);
        canvas.fillRect(kArtworkFrameX + 38, kArtworkFrameY + 18, 18, 44, theme.palette.progress);
        ::lofibox::ui::drawLine(canvas, kArtworkFrameX - 2, kArtworkFrameY + 8, kArtworkFrameX + 20, kArtworkFrameY - 2, theme.palette.focus_edge);
        ::lofibox::ui::drawLine(canvas, kArtworkFrameX - 2, kArtworkFrameY + 42, kArtworkFrameX + 20, kArtworkFrameY + 42, theme.palette.focus_edge);
        ::lofibox::ui::drawText(canvas, "NO ART", kArtworkFrameX + 12, kArtworkFrameY + 58, theme.palette.text_muted, 1);
    }

    ::lofibox::ui::drawText(canvas, view.title, 116, 30, theme.palette.text_primary, 1);
    ::lofibox::ui::drawText(canvas, view.artist, 116, 52, theme.palette.text_secondary, 1);
    ::lofibox::ui::drawText(canvas, view.album, 116, 68, theme.palette.text_muted, 1);

    ::lofibox::ui::drawFloatingProgressBar(
        canvas,
        theme,
        116,
        92,
        184,
        std::max(0, std::min(184, static_cast<int>((view.elapsed_seconds / std::max(1, view.duration_seconds)) * 184.0))));

    ::lofibox::ui::drawText(canvas, formatDuration(static_cast<int>(view.elapsed_seconds)), 116, 102, theme.palette.text_secondary, 1);
    ::lofibox::ui::drawText(canvas, formatDuration(view.duration_seconds), 262, 102, theme.palette.text_secondary, 1);

    const std::string play_label = view.status == NowPlayingStatus::Playing ? "PAUSE" : "PLAY";
    ::lofibox::ui::drawText(canvas, "PREV", 110, 124, theme.palette.text_muted, 1);
    ::lofibox::ui::drawText(canvas, play_label, 146, 124, theme.palette.text_primary, 1);
    ::lofibox::ui::drawText(canvas, "NEXT", 192, 124, theme.palette.text_muted, 1);
    ::lofibox::ui::drawText(canvas, view.shuffle_enabled ? "SHUF*" : "SHUF", 236, 124, view.shuffle_enabled ? theme.palette.progress : theme.palette.text_muted, 1);
    const std::string repeat_label = view.repeat_one ? "ONE*" : (view.repeat_all ? "REP*" : "REP");
    ::lofibox::ui::drawText(canvas, repeat_label, 278, 124, view.repeat_one || view.repeat_all ? theme.palette.progress : theme.palette.text_muted, 1);

    if (view.effect_name != "OFF") {
        const auto remix_label = ::lofibox::ui::fitUpper(view.effect_name, 7);
        constexpr int kWidth = 320;
        const int remix_x = kWidth - static_cast<int>(remix_label.size()) * 7 - 10;
        ::lofibox::ui::drawText(canvas, remix_label, remix_x, 4, theme.palette.focus_edge, 1);
    }

    renderTidalSpectrum(canvas, view, theme);
}

} // namespace lofibox::ui::pages
