// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "plugins/plugin_manifest.h"

namespace lofibox::plugins {

// Parse a single plugin.json file into a PluginManifest.
// Returns nullopt on hard rejection, with error_message populated.
[[nodiscard]] DiscoveredPlugin parseManifestFromFile(const std::filesystem::path& json_path);

// Parse a JSON string into a PluginManifest.
[[nodiscard]] DiscoveredPlugin parseManifestFromString(std::string_view json, std::string_view source_hint = {});

// Scan a directory for subdirectories containing plugin.json files.
// Returns one DiscoveredPlugin per found manifest (including failures).
[[nodiscard]] std::vector<DiscoveredPlugin> scanPluginDirectory(const std::filesystem::path& dir);

// Build a dependency graph from a set of discovered plugins.
// Only valid (Ok) plugins are included as nodes. Failed plugins are in skipped.
[[nodiscard]] DependencyGraph buildDependencyGraph(
    const std::vector<DiscoveredPlugin>& discovered);

// Topological sort using Kahn's algorithm.
// Returns indices into graph.plugins in activation order.
// Cyclic dependencies are detected; all nodes in a cycle are skipped
// and warnings appended to the output parameter.
[[nodiscard]] std::vector<std::size_t> topologicalSort(
    const DependencyGraph& graph,
    std::vector<std::string>& warnings);

// Resolve a dependency string: check by plugin_id first, then by capability.
[[nodiscard]] std::optional<std::size_t> resolveDependency(
    const PluginDependency& dep,
    const std::vector<DiscoveredPlugin>& plugins);

// Check if a capability string is in the recognized namespace.
[[nodiscard]] bool isRecognizedCapability(std::string_view cap) noexcept;

} // namespace lofibox::plugins
