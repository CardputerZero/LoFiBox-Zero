// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <string_view>

namespace lofibox::webui {

// Returns the embedded index.html (full SPA frontend).
const char* webUiIndexHtml() noexcept;

// Returns the embedded CSS.
const char* webUiCss() noexcept;

// Returns the embedded JavaScript.
const char* webUiJs() noexcept;

// Returns the MIME type for a given asset path.
// Recognised extensions: .html, .css, .js, .svg, .png, .ico
const char* mimeTypeForPath(std::string_view path) noexcept;

// Returns the embedded asset content for a given path, or nullptr if not found.
const char* assetContentForPath(std::string_view path) noexcept;

} // namespace lofibox::webui
