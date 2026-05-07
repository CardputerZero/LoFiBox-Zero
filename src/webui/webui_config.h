// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstdint>
#include <string>

namespace lofibox::webui {

struct WebUiConfig {
    bool enabled{false};
    std::string bind_address{"0.0.0.0"};
    std::uint16_t port{8765};
};

// Parse WebUI configuration from CLI arguments.
// Returns the number of arguments consumed (0 if none matched).
// Recognised forms:
//   --webui
//   --webui-bind <address>
//   --webui-port <port>
int parseWebUiCliArgs(int argc, char** argv, int start_index, WebUiConfig& config);

// Parse WebUI configuration from environment variables:
//   LOFIBOX_WEBUI=1
//   LOFIBOX_WEBUI_BIND=127.0.0.1
//   LOFIBOX_WEBUI_PORT=8765
void parseWebUiEnv(WebUiConfig& config);

// Convenience: parse environment + scan all CLI args (stops at first non-WebUI token,
// advances past unrecognised tokens so interleaved flags work).
void parseWebUiFromArgs(int argc, char** argv, WebUiConfig& config);

} // namespace lofibox::webui
