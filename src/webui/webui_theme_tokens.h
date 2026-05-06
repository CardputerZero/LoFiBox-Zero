// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <string_view>

namespace lofibox::webui {

// CSS custom property definitions matching the LoFiBox visual identity.
// Warm amber accent on deep black-blue — a large-screen remote panel,
// not a modern admin dashboard.
inline constexpr std::string_view kWebUiThemeCss = R"css(
:root {
  --lb-bg:          #07090d;
  --lb-panel:       #111722;
  --lb-panel-soft:  #151d2a;
  --lb-line:        #293241;
  --lb-text:        #f2efe6;
  --lb-muted:       #8b94a5;
  --lb-accent:      #e6a33a;
  --lb-accent-soft: #7a5420;
  --lb-danger:      #c65a4a;
  --lb-ok:          #7fb069;
}
)css";

} // namespace lofibox::webui
