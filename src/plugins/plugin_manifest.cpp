// SPDX-License-Identifier: GPL-3.0-or-later

#include "plugins/plugin_manifest.h"
#include "plugins/plugin_discovery.h"

#include <algorithm>
#include <set>
#include <string>

namespace lofibox::plugins {

// ---------------------------------------------------------------------------
// PluginRegistry
// ---------------------------------------------------------------------------

void PluginRegistry::clear()
{
    manifests_.clear();
    warnings_.clear();
}

void PluginRegistry::registerPlugin(PluginManifest manifest)
{
    if (findById(manifest.id) != nullptr) {
        warnings_.push_back("Duplicate plugin id '" + manifest.id + "'. Skipping.");
        return;
    }
    manifests_.push_back(std::move(manifest));
}

const std::vector<PluginManifest>& PluginRegistry::manifests() const noexcept
{
    return manifests_;
}

const PluginManifest* PluginRegistry::findById(std::string_view id) const noexcept
{
    auto it = std::find_if(manifests_.begin(), manifests_.end(),
        [id](const PluginManifest& m) { return m.id == id; });
    return it == manifests_.end() ? nullptr : &*it;
}

std::vector<const PluginManifest*> PluginRegistry::findByCapability(std::string_view capability) const noexcept
{
    std::vector<const PluginManifest*> result;
    for (const auto& m : manifests_) {
        if (manifestHasCapability(m, capability)) {
            result.push_back(&m);
        }
    }
    return result;
}

std::vector<const PluginManifest*> PluginRegistry::findByKind(PluginKind kind) const noexcept
{
    std::vector<const PluginManifest*> result;
    for (const auto& m : manifests_) {
        if (m.kind == kind) {
            result.push_back(&m);
        }
    }
    return result;
}

// ---------------------------------------------------------------------------
// Discovery pipeline
// ---------------------------------------------------------------------------

std::vector<DiscoveredPlugin> PluginRegistry::loadAllManifests(
    const std::vector<std::filesystem::path>& search_dirs) const
{
    std::vector<DiscoveredPlugin> all;

    // Track seen IDs to detect duplicates across directories
    std::set<std::string> seen_ids;

    for (const auto& dir : search_dirs) {
        auto found = scanPluginDirectory(dir);
        for (auto& d : found) {
            if (d.status == ManifestLoadStatus::Ok) {
                if (seen_ids.count(d.manifest.id)) {
                    d.status = ManifestLoadStatus::FieldInvalid;
                    d.error_message = "Duplicate plugin id '" + d.manifest.id + "' (already loaded from another directory)";
                } else {
                    seen_ids.insert(d.manifest.id);
                }
            }
            all.push_back(std::move(d));
        }
    }
    return all;
}

DependencyGraph PluginRegistry::buildDependencyGraph(
    std::vector<DiscoveredPlugin> discovered) const
{
    return plugins::buildDependencyGraph(discovered);
}

std::vector<std::size_t> PluginRegistry::topologicalSort(
    const DependencyGraph& graph)
{
    return plugins::topologicalSort(graph, warnings_);
}

void PluginRegistry::activateInOrder(
    const std::vector<std::size_t>& order,
    const DependencyGraph& graph)
{
    for (auto idx : order) {
        bool skipped = std::find(graph.skipped.begin(), graph.skipped.end(), idx) != graph.skipped.end();
        if (skipped) continue;

        const auto& discovered = graph.plugins[idx];
        if (discovered.status != ManifestLoadStatus::Ok) continue;

        // Check for unrecognized capabilities (soft warning)
        for (const auto& cap : discovered.manifest.capabilities) {
            if (!isRecognizedCapability(cap)) {
                warnings_.push_back("Plugin '" + discovered.manifest.id +
                    "': unrecognized capability '" + cap + "'");
            }
        }

        // Check for missing optional runtime dependencies
        for (const auto& dep : discovered.manifest.runtime_dependencies) {
            // Just log; we don't probe the system for installed packages.
            (void)dep;
        }

        registerPlugin(discovered.manifest);
    }

    // Log skipped plugins
    for (auto idx : graph.skipped) {
        if (idx < graph.plugins.size()) {
            const auto& d = graph.plugins[idx];
            warnings_.push_back("Plugin '" + d.manifest.id + "' skipped: " + d.error_message);
        }
    }
}

std::vector<DiscoveredPlugin> PluginRegistry::discover(
    const std::vector<std::filesystem::path>& search_dirs)
{
    clear();

    // Phase 1: Load all manifests
    auto discovered = loadAllManifests(search_dirs);

    // Phase 2: Build dependency graph
    auto graph = buildDependencyGraph(std::move(discovered));

    // Phase 3: Topological sort
    auto order = topologicalSort(graph);

    // Phase 4: Activate in order
    activateInOrder(order, graph);

    return graph.plugins;
}

} // namespace lofibox::plugins
