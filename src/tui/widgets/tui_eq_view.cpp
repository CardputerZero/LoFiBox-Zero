// SPDX-License-Identifier: GPL-3.0-or-later

#include "tui/widgets/tui_eq_view.h"

#include <algorithm>
#include <sstream>

#include "tui/widgets/tui_text.h"

namespace lofibox::tui::widgets {

std::vector<std::string> renderEqView(const runtime::EqRuntimeSnapshot& eq, int width, TuiCharset charset)
{
    std::vector<std::string> lines{};
    const std::string block = charset == TuiCharset::Ascii ? "#" : "█";
    lines.push_back(fitText("EQ: " + eq.preset_name + (eq.enabled ? " ON" : " OFF")
        + "  REMIX: " + eq.effect_name, width));
    std::ostringstream labels{};
    labels << "31 63 125 250 500 1k 2k 4k 8k 16k";
    lines.push_back(fitText(labels.str(), width));
    std::string gains{};
    for (const int gain : eq.bands) {
        const int height = std::clamp(gain + 12, 0, 24) / 4;
        if (height == 0) {
            gains += ".";
        } else {
            for (int index = 0; index < height; ++index) {
                gains += block;
            }
        }
        gains += ' ';
    }
    lines.push_back(fitText(gains, width));
    return lines;
}

} // namespace lofibox::tui::widgets
