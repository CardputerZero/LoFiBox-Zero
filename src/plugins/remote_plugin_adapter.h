// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "app/remote_media_services.h"
#include "plugins/plugin_manifest.h"
#include "plugins/plugin_runtime.h"

namespace lofibox::plugins {

class RemotePluginAdapter : public app::RemoteSourceProvider,
                            public app::RemoteCatalogProvider,
                            public app::RemoteStreamResolver {
public:
    explicit RemotePluginAdapter(PluginManifest manifest,
                                 PluginRuntime runtime);

    // RemoteSourceProvider
    [[nodiscard]] bool available() const override;
    [[nodiscard]] std::string displayName() const override;
    [[nodiscard]] app::RemoteSourceSession probe(const app::RemoteServerProfile& profile) const override;

    // RemoteCatalogProvider
    [[nodiscard]] std::vector<app::RemoteTrack> searchTracks(
        const app::RemoteServerProfile& profile,
        const app::RemoteSourceSession& session,
        std::string_view query,
        int limit) const override;
    [[nodiscard]] std::vector<app::RemoteTrack> recentTracks(
        const app::RemoteServerProfile& profile,
        const app::RemoteSourceSession& session,
        int limit) const override;
    [[nodiscard]] std::vector<app::RemoteTrack> libraryTracks(
        const app::RemoteServerProfile& profile,
        const app::RemoteSourceSession& session,
        int limit) const override;
    [[nodiscard]] std::vector<app::RemoteCatalogNode> browse(
        const app::RemoteServerProfile& profile,
        const app::RemoteSourceSession& session,
        const app::RemoteCatalogNode& parent,
        int limit) const override;

    // RemoteStreamResolver
    [[nodiscard]] std::optional<app::ResolvedRemoteStream> resolveTrack(
        const app::RemoteServerProfile& profile,
        const app::RemoteSourceSession& session,
        const app::RemoteTrack& track) const override;

    // Plugin-specific
    [[nodiscard]] const PluginManifest& manifest() const noexcept { return manifest_; }
    [[nodiscard]] std::string profileSchemaJson() const;
    [[nodiscard]] app::RemoteServerKind serverKind() const;

private:
    std::string profileJson(const app::RemoteServerProfile& profile) const;
    std::string sessionJson(const app::RemoteSourceSession& session) const;
    std::string trackJson(const app::RemoteTrack& track) const;
    std::string parentNodeJson(const app::RemoteCatalogNode& parent) const;

    std::vector<app::RemoteTrack> parseTracksResponse(std::string_view json) const;
    std::vector<app::RemoteCatalogNode> parseNodesResponse(std::string_view json) const;
    std::optional<app::ResolvedRemoteStream> parseStreamResponse(std::string_view json) const;
    app::RemoteSourceSession parseSessionResponse(std::string_view json) const;

    PluginManifest manifest_;
    mutable PluginRuntime runtime_;
};

class RemotePluginRegistry {
public:
    void registerAdapter(std::unique_ptr<RemotePluginAdapter> adapter);
    [[nodiscard]] RemotePluginAdapter* findForKind(app::RemoteServerKind kind) const;
    [[nodiscard]] RemotePluginAdapter* findForKindString(std::string_view kind_str) const;
    [[nodiscard]] const std::vector<std::unique_ptr<RemotePluginAdapter>>& adapters() const noexcept;

    // Discover plugins by scanning directories
    void discoverFromDir(const std::filesystem::path& dir);

private:
    std::vector<std::unique_ptr<RemotePluginAdapter>> adapters_{};
};

} // namespace lofibox::plugins
