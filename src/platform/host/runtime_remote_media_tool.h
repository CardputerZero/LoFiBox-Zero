// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "app/remote_media_services.h"
#include "plugins/plugin_manifest.h"
#include "plugins/plugin_runtime.h"

namespace lofibox::platform::host::runtime_detail {
namespace fs = std::filesystem;

class RemoteMediaToolClient {
public:
    RemoteMediaToolClient();
    [[nodiscard]] bool available() const;
    [[nodiscard]] app::RemoteSourceSession probe(const app::RemoteServerProfile& profile) const;
    [[nodiscard]] std::vector<app::RemoteTrack> searchTracks(
        const app::RemoteServerProfile& profile,
        const app::RemoteSourceSession& session,
        std::string_view query,
        int limit) const;
    [[nodiscard]] std::vector<app::RemoteTrack> recentTracks(
        const app::RemoteServerProfile& profile,
        const app::RemoteSourceSession& session,
        int limit) const;
    [[nodiscard]] std::vector<app::RemoteTrack> libraryTracks(
        const app::RemoteServerProfile& profile,
        const app::RemoteSourceSession& session,
        int limit) const;
    [[nodiscard]] std::vector<app::RemoteCatalogNode> browse(
        const app::RemoteServerProfile& profile,
        const app::RemoteSourceSession& session,
        const app::RemoteCatalogNode& parent,
        int limit) const;
    [[nodiscard]] std::optional<app::ResolvedRemoteStream> resolveTrack(
        const app::RemoteServerProfile& profile,
        const app::RemoteSourceSession& session,
        const app::RemoteTrack& track) const;

private:
    [[nodiscard]] const plugins::PluginManifest* pluginForProfile(const app::RemoteServerProfile& profile) const;
    [[nodiscard]] plugins::PluginRuntime* runtimeForPlugin(const plugins::PluginManifest& manifest) const;
    [[nodiscard]] std::optional<std::string> callPlugin(
        const app::RemoteServerProfile& profile,
        std::string_view method,
        std::string_view params_json) const;

    std::optional<fs::path> python_path_{};
    std::vector<plugins::PluginManifest> remote_plugins_{};
    mutable std::unordered_map<std::string, std::unique_ptr<plugins::PluginRuntime>> runtimes_{};
};

} // namespace lofibox::platform::host::runtime_detail
