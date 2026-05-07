// SPDX-License-Identifier: GPL-3.0-or-later

#include "tui/widgets/tui_help_overlay.h"

#include "tui/widgets/tui_text.h"

namespace lofibox::tui::widgets {

std::vector<std::string> helpOverlayLines(int width)
{
    return {
        fitText("LoFiBox TUI Help", width),
        fitText("Space play/pause  n next  p previous  s stop", width),
        fitText("Left/Right seek  r reconnect  R remix  e toggle EQ", width),
        fitText("1 dashboard 2 now 3 lyrics 4 spectrum 5 queue", width),
        fitText(": command palette  / search  q quit TUI only", width),
    };
}

} // namespace lofibox::tui::widgets
