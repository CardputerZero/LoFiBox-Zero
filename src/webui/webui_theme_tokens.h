// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstdio>
#include <string>
#include <string_view>

#include "ui/ui_theme.h"

namespace lofibox::webui {

// Maps all 17 UiPalette colors to CSS custom properties.
// Semantic naming follows the TUI palette roles, not 1:1 field names.
//
// Mapping:
//   background      -> --lb-bg
//   panel0          -> --lb-panel-deep
//   panel1          -> --lb-panel
//   panel2          -> --lb-panel-soft
//   chrome_topbar0  -> --lb-chrome
//   chrome_topbar1  -> --lb-chrome-dim
//   divider         -> --lb-line
//   text_primary    -> --lb-text
//   text_secondary  -> --lb-text-soft
//   text_muted      -> --lb-muted
//   focus_fill      -> --lb-focus
//   focus_edge      -> --lb-focus-edge
//   progress        -> --lb-accent
//   progress_strong -> --lb-accent-strong
//   good            -> --lb-ok
//   warn            -> --lb-warn
//   bad             -> --lb-danger

[[nodiscard]] inline std::string buildThemeCss(const ui::UiTheme& theme)
{
    const auto& p = theme.palette;
    char buf[2048]{};
    std::snprintf(buf, sizeof(buf), R"css(
:root {
  --lb-bg:           #%02x%02x%02x;
  --lb-panel-deep:   #%02x%02x%02x;
  --lb-panel:        #%02x%02x%02x;
  --lb-panel-soft:   #%02x%02x%02x;
  --lb-chrome:       #%02x%02x%02x;
  --lb-chrome-dim:   #%02x%02x%02x;
  --lb-line:         #%02x%02x%02x;
  --lb-text:         #%02x%02x%02x;
  --lb-text-soft:    #%02x%02x%02x;
  --lb-muted:        #%02x%02x%02x;
  --lb-focus:        #%02x%02x%02x;
  --lb-focus-edge:   #%02x%02x%02x;
  --lb-accent:       #%02x%02x%02x;
  --lb-accent-strong:#%02x%02x%02x;
  --lb-accent-soft:  #%02x%02x%02x;
  --lb-ok:           #%02x%02x%02x;
  --lb-warn:         #%02x%02x%02x;
  --lb-danger:       #%02x%02x%02x;
}
)css",
        p.background.r,    p.background.g,    p.background.b,
        p.panel0.r,        p.panel0.g,        p.panel0.b,
        p.panel1.r,        p.panel1.g,        p.panel1.b,
        p.panel2.r,        p.panel2.g,        p.panel2.b,
        p.chrome_topbar0.r,p.chrome_topbar0.g,p.chrome_topbar0.b,
        p.chrome_topbar1.r,p.chrome_topbar1.g,p.chrome_topbar1.b,
        p.divider.r,       p.divider.g,       p.divider.b,
        p.text_primary.r,  p.text_primary.g,  p.text_primary.b,
        p.text_secondary.r,p.text_secondary.g,p.text_secondary.b,
        p.text_muted.r,    p.text_muted.g,    p.text_muted.b,
        p.focus_fill.r,    p.focus_fill.g,    p.focus_fill.b,
        p.focus_edge.r,    p.focus_edge.g,    p.focus_edge.b,
        p.progress.r,      p.progress.g,      p.progress.b,
        p.progress_strong.r,p.progress_strong.g,p.progress_strong.b,
        p.progress_strong.r,p.progress_strong.g,p.progress_strong.b,
        p.good.r,          p.good.g,          p.good.b,
        p.warn.r,          p.warn.g,          p.warn.b,
        p.bad.r,           p.bad.g,           p.bad.b);
    return std::string(buf);
}

} // namespace lofibox::webui
