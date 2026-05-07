// SPDX-License-Identifier: GPL-3.0-or-later

#include "platform/host/runtime_services_factory.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "platform/host/runtime_host_internal.h"
#include "platform/host/runtime_paths.h"
#include "platform/host/host_runtime_service_providers.h"
#include "plugins/plugin_manifest.h"
#include "plugins/skin_plugin_adapter.h"

namespace lofibox::platform::host {
namespace {

std::optional<std::string> extractJsonString(std::string_view json, std::string_view marker)
{
    auto pos = json.find(marker);
    if (pos == std::string_view::npos) return std::nullopt;
    pos += marker.size();
    std::string out;
    while (pos < json.size() && json[pos] != '"') {
        if (json[pos] == '\\' && pos + 1 < json.size()) {
            ++pos;
        }
        out.push_back(json[pos++]);
    }
    return out;
}

std::string selectedSkinId()
{
    const auto path = runtime_paths::appConfigDir() / "plugins.json";
    std::ifstream file(path, std::ios::in | std::ios::binary);
    if (!file.is_open()) return {};
    std::stringstream buffer;
    buffer << file.rdbuf();
    return extractJsonString(buffer.str(), "\"selected_skin\":\"").value_or(
        extractJsonString(buffer.str(), "\"selected_skin\": \"").value_or(""));
}

std::vector<std::filesystem::path> pluginSearchDirs()
{
    std::vector<std::filesystem::path> dirs;
    if (const char* env = std::getenv("LOFIBOX_PLUGIN_DIR"); env != nullptr && *env != '\0') {
        dirs.emplace_back(env);
    }
    dirs.push_back(runtime_detail::projectRoot() / "data" / "plugins");
    dirs.push_back(runtime_paths::appDataDir() / "plugins");
#if !defined(_WIN32)
    dirs.emplace_back("/usr/share/lofibox/plugins");
#endif
    return dirs;
}

std::shared_ptr<ui::UiTheme> loadHostTheme()
{
    auto theme = std::make_shared<ui::UiTheme>(ui::defaultTheme());
    plugins::PluginRegistry registry;
    registry.discover(pluginSearchDirs());

    const auto selected = selectedSkinId();
    const plugins::PluginManifest* manifest = nullptr;
    if (!selected.empty()) {
        manifest = registry.findById(selected);
    }
    if (manifest == nullptr) {
        for (const auto* candidate : registry.findByCapability("ui.theme")) {
            if (candidate->kind == plugins::PluginKind::AssetPack) {
                manifest = candidate;
                break;
            }
        }
    }
    if (manifest == nullptr) {
        return theme;
    }

    plugins::SkinPluginAdapter adapter;
    if (adapter.loadSkinFromDir(manifest->source_dir)) {
        *theme = adapter.theme();
        theme->id = manifest->id;
    }
    return theme;
}

app::PluginServices loadHostPluginServices(const ui::UiTheme& active_theme)
{
    plugins::PluginRegistry registry;
    registry.discover(pluginSearchDirs());

    app::PluginServices services{};
    services.selected_skin_id = active_theme.id;
    services.warnings = registry.warnings();
    for (const auto& manifest : registry.manifests()) {
        services.loaded_plugin_ids.push_back(manifest.id);
    }
    return services;
}

} // namespace

app::RuntimeServices createHostRuntimeServices()
{
    auto context = createHostRuntimeServiceContext();
    app::RuntimeServices services{};
    services.connectivity = createHostConnectivityServices(context);
    services.metadata = createHostMetadataServices(context);
    services.playback = createHostPlaybackServices(context);
    services.remote = createHostRemoteMediaServices(context);
    services.cache = createHostCacheServices(context);
    services.ui.theme = loadHostTheme();
    services.plugins = loadHostPluginServices(*services.ui.theme);
    return app::withNullRuntimeServices(std::move(services));
}

} // namespace lofibox::platform::host
