// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui/ui_primitives.h"

#include <algorithm>
#include <cctype>
#include <cmath>

#include "core/bitmap_font.h"
#include "core/display_profile.h"

namespace lofibox::ui {
namespace {

using core::rgba;

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

void fillCircle(core::Canvas& canvas, int center_x, int center_y, int radius, core::Color color)
{
    for (int y = -radius; y <= radius; ++y) {
        for (int x = -radius; x <= radius; ++x) {
            if ((x * x) + (y * y) <= (radius * radius)) {
                canvas.setPixel(center_x + x, center_y + y, color);
            }
        }
    }
}

} // namespace

std::string upperText(std::string_view text)
{
    std::string result{};
    result.reserve(text.size());
    for (const unsigned char ch : text) {
        if (ch < 0x80U) {
            result.push_back(static_cast<char>(std::toupper(ch)));
        } else {
            result.push_back(static_cast<char>(ch));
        }
    }
    return result;
}

std::string fitText(std::string_view text, std::size_t max_chars)
{
    std::string result{text};
    std::size_t char_count = 0;
    for (const unsigned char ch : result) {
        if ((ch & 0xC0U) != 0x80U) {
            ++char_count;
        }
    }

    if (char_count <= max_chars) {
        return result;
    }

    if (max_chars <= 3) {
        std::size_t bytes = 0;
        std::size_t seen = 0;
        while (bytes < result.size() && seen < max_chars) {
            const unsigned char ch = static_cast<unsigned char>(result[bytes]);
            if ((ch & 0xC0U) != 0x80U) {
                ++seen;
            }
            ++bytes;
        }
        return result.substr(0, bytes);
    }

    std::size_t bytes = 0;
    std::size_t seen = 0;
    const std::size_t keep_chars = max_chars - 3;
    while (bytes < result.size() && seen < keep_chars) {
        const unsigned char ch = static_cast<unsigned char>(result[bytes]);
        if ((ch & 0xC0U) != 0x80U) {
            ++seen;
        }
        ++bytes;
    }

    while (bytes < result.size() && (static_cast<unsigned char>(result[bytes]) & 0xC0U) == 0x80U) {
        ++bytes;
    }

    return result.substr(0, bytes) + "...";
}

std::string fitUpper(std::string_view text, std::size_t max_chars)
{
    return fitText(upperText(text), max_chars);
}

int centeredX(std::string_view text, int scale) noexcept
{
    return (core::kDisplayWidth - core::bitmap_font::measureText(text, scale)) / 2;
}

int centeredTextY(int region_y, int region_height, int scale) noexcept
{
    return region_y + std::max(0, (region_height - core::bitmap_font::lineHeight(scale)) / 2);
}

void drawText(core::Canvas& canvas, std::string_view text, int x, int y, core::Color color, int scale)
{
    core::bitmap_font::drawText(canvas, text, x, y, color, scale);
}

void drawFadedTextWindow(
    core::Canvas& canvas,
    std::string_view text,
    int x,
    int y,
    int width,
    int scroll_px,
    core::Color color,
    bool right_align_when_fits)
{
    if (width <= 0 || text.empty()) {
        return;
    }

    constexpr int kScale = 1;
    constexpr int kFadeWidth = 18;
    constexpr int kScrollGap = 28;
    const int text_width = core::bitmap_font::measureText(text, kScale);
    const int text_height = core::bitmap_font::lineHeight(kScale);
    if (text_width <= 0 || text_height <= 0) {
        return;
    }

    core::Canvas source{width, text_height + 2};
    source.clear(rgba(0, 0, 0, 0));
    if (text_width <= width - 4) {
        const int text_x = right_align_when_fits ? std::max(0, width - text_width) : 0;
        drawText(source, text, text_x, 0, color, kScale);
    } else {
        const int cycle_width = width + text_width + kScrollGap;
        const int offset = ((scroll_px % cycle_width) + cycle_width) % cycle_width;
        const int text_x = width - offset;
        drawText(source, text, text_x, 0, color, kScale);
        drawText(source, text, text_x + cycle_width, 0, color, kScale);
    }

    for (int row = 0; row < source.height(); ++row) {
        for (int col = 0; col < width; ++col) {
            const auto pixel = source.pixel(col, row);
            if (pixel.a == 0) {
                continue;
            }

            const float left_fade = std::clamp(static_cast<float>(col) / static_cast<float>(kFadeWidth), 0.0f, 1.0f);
            const float right_fade = std::clamp(static_cast<float>(width - 1 - col) / static_cast<float>(kFadeWidth), 0.0f, 1.0f);
            const float edge_fade = std::min(left_fade, right_fade);
            const auto opacity = static_cast<std::uint8_t>(std::clamp(edge_fade * 220.0f, 0.0f, 220.0f));
            if (opacity > 0) {
                blendPixel(canvas, x + col, y + row, pixel, opacity);
            }
        }
    }
}

void drawTopBar(
    core::Canvas& canvas,
    const UiTheme& theme,
    std::string_view title,
    bool show_back,
    std::string_view left_hint,
    std::string_view right_hint) noexcept
{
    (void)show_back;
    const int bar_h = theme.spacing.top_bar_height;
    canvas.fillRect(0, 0, core::kDisplayWidth, bar_h, theme.palette.chrome_topbar0);
    canvas.fillRect(0, 12, core::kDisplayWidth, 7, theme.palette.chrome_topbar1);
    canvas.fillRect(0, bar_h - 1, core::kDisplayWidth, 1, theme.palette.divider);

    const int text_y = centeredTextY(0, bar_h, 1);
    drawText(canvas, left_hint.empty() ? std::string_view{"F1:HELP"} : left_hint, 6, text_y, theme.palette.text_secondary, 1);

    drawText(canvas, title, centeredX(upperText(title), 1), text_y, theme.palette.text_primary, 1);

    if (!right_hint.empty()) {
        const auto label = fitUpper(right_hint, 10);
        const int width = core::bitmap_font::measureText(label, 1);
        drawText(canvas, label, std::max(206, core::kDisplayWidth - width - 6), text_y, theme.palette.text_secondary, 1);
    }
}

void drawListPageFrame(core::Canvas& canvas, const UiTheme& theme)
{
    canvas.fillRect(0, 0, core::kDisplayWidth, core::kDisplayHeight, theme.palette.background);
    canvas.fillRect(0, theme.spacing.top_bar_height, core::kDisplayWidth, core::kDisplayHeight - theme.spacing.top_bar_height, theme.palette.panel0);
}

void drawPageHelpModal(core::Canvas& canvas, const UiTheme& theme, std::string_view title, const std::vector<std::pair<std::string_view, std::string_view>>& rows)
{
    constexpr int x = 14;
    constexpr int y = 22;
    constexpr int width = 292;
    constexpr int height = 142;

    canvas.fillRect(x + 4, y + 5, width, height, rgba(0, 0, 0));
    canvas.fillRect(x, y, width, height, rgba(12, 16, 22));
    canvas.strokeRect(x, y, width, height, rgba(116, 196, 255), 1);

    for (int col = 4; col < width - 4; ++col) {
        const float edge = static_cast<float>(std::min(col, width - 1 - col)) / 34.0f;
        const auto opacity = static_cast<std::uint8_t>(std::clamp(edge, 0.0f, 1.0f) * 96.0f);
        const auto top = alphaBlend(canvas.pixel(x + col, y + 3), rgba(148, 221, 255), opacity);
        const auto bottom = alphaBlend(canvas.pixel(x + col, y + height - 4), rgba(31, 91, 180), static_cast<std::uint8_t>(opacity * 0.45f));
        canvas.setPixel(x + col, y + 3, top);
        canvas.setPixel(x + col, y + height - 4, bottom);
    }

    drawText(canvas, title, centeredX(title, 1), y + 8, theme.palette.text_primary, 1);
    if (rows.empty()) {
        drawText(canvas, "NO SHORTCUTS", centeredX("NO SHORTCUTS", 1), y + 54, theme.palette.text_muted, 1);
        return;
    }

    int row_y = y + 24;
    for (const auto& row : rows) {
        drawText(canvas, fitUpper(row.first, 11), x + 16, row_y, theme.palette.progress_strong, 1);
        drawText(canvas, fitUpper(row.second, 24), x + 90, row_y, theme.palette.text_secondary, 1);
        row_y += 11;
        if (row_y > y + height - 10) {
            break;
        }
    }
}

void drawGlassListFocus(core::Canvas& canvas, const UiTheme& /*theme*/, int x, int y, int width, int height)
{
    if (width <= 0 || height <= 0) {
        return;
    }

    const float half_h = std::max(1.0f, static_cast<float>(height) / 2.0f);
    for (int row = 0; row < height; ++row) {
        const float distance = std::abs((static_cast<float>(row) + 0.5f) - half_h);
        const float vertical = std::clamp(1.0f - (distance / half_h), 0.0f, 1.0f);
        const float smooth_vertical = vertical * vertical * (3.0f - (2.0f * vertical));
        for (int col = 0; col < width; ++col) {
            const float edge = static_cast<float>(std::min(col, width - 1 - col)) / 26.0f;
            const float horizontal = std::clamp(edge, 0.0f, 1.0f);
            const float center_lift = 0.68f + (0.32f * horizontal);
            const auto cool_opacity = static_cast<std::uint8_t>(118.0f * smooth_vertical * center_lift);
            const auto blue_opacity = static_cast<std::uint8_t>(76.0f * smooth_vertical * horizontal);
            if (cool_opacity > 0) {
                blendPixel(canvas, x + col, y + row, rgba(70, 156, 255), cool_opacity);
            }
            if (blue_opacity > 0) {
                blendPixel(canvas, x + col, y + row, rgba(146, 219, 255), blue_opacity);
            }
        }
    }

    const int highlight_y = y + std::max(1, height / 4);
    const int shadow_y = y + height - std::max(2, height / 4);
    for (int col = 3; col < width - 3; ++col) {
        const float edge = static_cast<float>(std::min(col, width - 1 - col)) / 32.0f;
        const float horizontal = std::clamp(edge, 0.0f, 1.0f);
        blendPixel(canvas, x + col, highlight_y, rgba(224, 247, 255), static_cast<std::uint8_t>(82.0f * horizontal));
        blendPixel(canvas, x + col, shadow_y, rgba(38, 78, 186), static_cast<std::uint8_t>(48.0f * horizontal));
    }
}

void drawGlassScrollbar(core::Canvas& canvas, const UiTheme& /*theme*/, int x, int y, int width, int height, int thumb_y, int thumb_height)
{
    if (width <= 0 || height <= 0 || thumb_height <= 0) {
        return;
    }

    const int track_center_x = x + (width / 2);
    for (int row = 0; row < height; ++row) {
        const float edge_y = static_cast<float>(std::min(row, height - 1 - row)) / 12.0f;
        const float vertical_fade = std::clamp(edge_y, 0.0f, 1.0f);
        for (int col = 0; col < width; ++col) {
            const float distance_x = std::abs(static_cast<float>((x + col) - track_center_x));
            const float horizontal = std::clamp(1.0f - (distance_x / std::max(1.0f, static_cast<float>(width) / 2.0f)), 0.0f, 1.0f);
            const auto opacity = static_cast<std::uint8_t>(34.0f * horizontal * vertical_fade);
            if (opacity > 0) {
                blendPixel(canvas, x + col, y + row, rgba(78, 100, 126), opacity);
            }
        }
    }

    const int clamped_thumb_y = std::clamp(thumb_y, y, y + height - 1);
    const int clamped_thumb_h = std::clamp(thumb_height, 1, y + height - clamped_thumb_y);
    const float half_h = std::max(1.0f, static_cast<float>(clamped_thumb_h) / 2.0f);
    for (int row = 0; row < clamped_thumb_h; ++row) {
        const float distance_y = std::abs((static_cast<float>(row) + 0.5f) - half_h);
        const float vertical = std::clamp(1.0f - (distance_y / half_h), 0.0f, 1.0f);
        const float smooth_vertical = vertical * vertical * (3.0f - (2.0f * vertical));
        for (int col = 0; col < width; ++col) {
            const float distance_x = std::abs(static_cast<float>(col) - (static_cast<float>(width - 1) / 2.0f));
            const float horizontal = std::clamp(1.0f - (distance_x / std::max(1.0f, static_cast<float>(width) / 2.0f)), 0.0f, 1.0f);
            const auto blue_opacity = static_cast<std::uint8_t>(150.0f * smooth_vertical * horizontal);
            const auto core_opacity = static_cast<std::uint8_t>(112.0f * smooth_vertical * horizontal * horizontal);
            if (blue_opacity > 0) {
                blendPixel(canvas, x + col, clamped_thumb_y + row, rgba(52, 132, 255), blue_opacity);
            }
            if (core_opacity > 0) {
                blendPixel(canvas, x + col, clamped_thumb_y + row, rgba(196, 239, 255), core_opacity);
            }
        }
    }
}

void drawFloatingProgressBar(core::Canvas& canvas, const UiTheme& theme, int x, int y, int width, int filled_width)
{
    const int clamped_fill = std::clamp(filled_width, 0, width);
    const auto draw_faded_bar = [&](int bar_x, int bar_y, int bar_w, int bar_h, core::Color color, std::uint8_t max_opacity) {
        if (bar_w <= 0 || bar_h <= 0) {
            return;
        }
        for (int col = 0; col < bar_w; ++col) {
            const float edge = static_cast<float>(std::min(col, bar_w - 1 - col)) / 10.0f;
            const float horizontal = std::clamp(edge, 0.0f, 1.0f);
            for (int row = 0; row < bar_h; ++row) {
                const float half_h = std::max(1.0f, static_cast<float>(bar_h) / 2.0f);
                const float vertical = 1.0f - (std::abs((static_cast<float>(row) + 0.5f) - half_h) / half_h);
                const auto opacity = static_cast<std::uint8_t>(max_opacity * horizontal * std::clamp(vertical, 0.25f, 1.0f));
                blendPixel(canvas, bar_x + col, bar_y + row, color, opacity);
            }
        }
    };

    draw_faded_bar(x - 3, y + 3, width + 6, 6, rgba(0, 0, 0), 90);
    draw_faded_bar(x, y, width, 5, rgba(42, 55, 70), 105);
    draw_faded_bar(x, y - 1, clamped_fill, 7, theme.palette.progress_strong, 92);
    draw_faded_bar(x, y + 1, clamped_fill, 3, rgba(112, 202, 255), 175);
}

void drawSoftLyricFocus(core::Canvas& canvas, const UiTheme& /*theme*/, int x, int center_y, int width, int height)
{
    const float half = std::max(1.0f, static_cast<float>(height) / 2.0f);
    const int top = center_y - (height / 2);
    for (int row = 0; row < height; ++row) {
        const float distance = std::abs((static_cast<float>(row) + 0.5f) - half);
        const float vertical = std::clamp(1.0f - (distance / half), 0.0f, 1.0f);
        const auto row_opacity = static_cast<std::uint8_t>(vertical * vertical * 150.0f);
        if (row_opacity == 0) {
            continue;
        }
        for (int col = 0; col < width; ++col) {
            const float edge = static_cast<float>(std::min(col, width - 1 - col)) / 28.0f;
            const float horizontal = std::clamp(edge, 0.0f, 1.0f);
            blendPixel(canvas, x + col, top + row, rgba(48, 126, 224), static_cast<std::uint8_t>(row_opacity * horizontal));
        }
    }
}

void drawLine(core::Canvas& canvas, int x0, int y0, int x1, int y1, core::Color color, int thickness)
{
    const int dx = std::abs(x1 - x0);
    const int sx = x0 < x1 ? 1 : -1;
    const int dy = -std::abs(y1 - y0);
    const int sy = y0 < y1 ? 1 : -1;
    int error = dx + dy;

    while (true) {
        canvas.fillRect(x0 - (thickness / 2), y0 - (thickness / 2), thickness, thickness, color);
        if (x0 == x1 && y0 == y1) {
            break;
        }
        const int twice_error = error * 2;
        if (twice_error >= dy) {
            error += dy;
            x0 += sx;
        }
        if (twice_error <= dx) {
            error += dx;
            y0 += sy;
        }
    }
}

void drawPaginationDots(core::Canvas& canvas, const UiTheme& theme, int x, int y, int count, int selected)
{
    constexpr int kDotSpacing = 10;
    for (int index = 0; index < count; ++index) {
        const bool active = index == selected;
        const auto color = active ? theme.palette.progress_strong : theme.palette.panel2;
        fillCircle(canvas, x + (index * kDotSpacing), y, 2, color);
        if (active) {
            fillCircle(canvas, x + (index * kDotSpacing), y, 3, theme.palette.progress);
        }
    }
}

void blitScaledCanvas(
    core::Canvas& target,
    const core::Canvas& source,
    int dst_x,
    int dst_y,
    int dst_w,
    int dst_h,
    std::uint8_t opacity)
{
    if (dst_w <= 0 || dst_h <= 0 || source.width() <= 0 || source.height() <= 0) {
        return;
    }

    for (int y = 0; y < dst_h; ++y) {
        const int src_y = (y * source.height()) / dst_h;
        for (int x = 0; x < dst_w; ++x) {
            const int src_x = (x * source.width()) / dst_w;
            const auto src = source.pixel(src_x, src_y);
            if (src.a == 0 || opacity == 0) {
                continue;
            }

            const int tx = dst_x + x;
            const int ty = dst_y + y;
            const auto dst = target.pixel(tx, ty);
            target.setPixel(tx, ty, alphaBlend(dst, src, opacity));
        }
    }
}

core::Color mixColor(core::Color a, core::Color b, float t) noexcept
{
    const auto mix = [t](std::uint8_t lhs, std::uint8_t rhs) -> std::uint8_t {
        const float value = (static_cast<float>(lhs) * (1.0f - t)) + (static_cast<float>(rhs) * t);
        return static_cast<std::uint8_t>(std::clamp(value, 0.0f, 255.0f));
    };

    return rgba(mix(a.r, b.r), mix(a.g, b.g), mix(a.b, b.b), mix(a.a, b.a));
}

} // namespace lofibox::ui
