// SPDX-License-Identifier: GPL-3.0-or-later

#include "webui/webui_config.h"

#include <cstdlib>
#include <cstring>
#include <string_view>

namespace lofibox::webui {

int parseWebUiCliArgs(int argc, char** argv, int start_index, WebUiConfig& config)
{
    int consumed = 0;
    for (int index = start_index; index < argc; ++index) {
        const std::string_view current{argv[index]};
        if (current == "--webui") {
            config.enabled = true;
            ++consumed;
            continue;
        }
        if (current == "--webui-bind" && index + 1 < argc) {
            config.bind_address = argv[++index];
            consumed += 2;
            continue;
        }
        if (current == "--webui-port" && index + 1 < argc) {
            const auto port_str = std::string_view{argv[++index]};
            try {
                const int port = std::stoi(std::string{port_str});
                if (port > 0 && port <= 65535) {
                    config.port = static_cast<std::uint16_t>(port);
                }
            } catch (...) {}
            consumed += 2;
            continue;
        }
        break;
    }
    return consumed;
}

void parseWebUiEnv(WebUiConfig& config)
{
    if (const char* env = std::getenv("LOFIBOX_WEBUI"); env != nullptr) {
        const std::string_view value{env};
        if (value == "1" || value == "true" || value == "yes" || value == "on") {
            config.enabled = true;
        }
    }
    if (const char* env = std::getenv("LOFIBOX_WEBUI_BIND"); env != nullptr && *env != '\0') {
        config.bind_address = env;
    }
    if (const char* env = std::getenv("LOFIBOX_WEBUI_PORT"); env != nullptr && *env != '\0') {
        try {
            const int port = std::stoi(std::string{env});
            if (port > 0 && port <= 65535) {
                config.port = static_cast<std::uint16_t>(port);
            }
        } catch (...) {}
    }
}

void parseWebUiFromArgs(int argc, char** argv, WebUiConfig& config)
{
    parseWebUiEnv(config);
    for (int i = 1; i < argc; ) {
        int consumed = parseWebUiCliArgs(argc, argv, i, config);
        if (consumed > 0) { i += consumed; continue; }
        ++i;
    }
}

} // namespace lofibox::webui
