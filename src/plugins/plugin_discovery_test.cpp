// SPDX-License-Identifier: GPL-3.0-or-later
// Compile-time validation of topological sort.
// Run: add to test binary or call pluginDiscoverySelfTest() from main.

#include "plugins/plugin_discovery.h"

#include <cassert>
#include <iostream>
#include <set>
#include <string>
#include <vector>

namespace lofibox::plugins {
namespace {

DiscoveredPlugin makePlugin(const std::string& id, const std::string& name,
                             PluginKind kind,
                             const std::vector<std::string>& caps = {"remote.source"},
                             const std::vector<PluginDependency>& deps = {})
{
    DiscoveredPlugin d;
    d.status = ManifestLoadStatus::Ok;
    d.manifest.id = id;
    d.manifest.name = name;
    d.manifest.kind = kind;
    d.manifest.version = "1.0.0";
    d.manifest.api_version = "1";
    d.manifest.capabilities = caps;
    d.manifest.depends_on = deps;
    return d;
}

} // namespace

bool pluginDiscoverySelfTest()
{
    // Scenario: A (cache, internal) -> C (auth) -> B (jellyfin, depends on auth)
    // Expected order: A, C, B (or any topological order respecting A -> C -> B)

    std::vector<DiscoveredPlugin> discovered;

    // Plugin A: cache (no dependencies)
    discovered.push_back(makePlugin("a.cache", "Cache", PluginKind::InternalProvider, {"remote.source"}));

    // Plugin C: auth provider
    discovered.push_back(makePlugin("c.auth", "Auth", PluginKind::ExternalHelper, {"remote.auth.profile"}));

    // Plugin B: jellyfin (depends on auth capability, required)
    discovered.push_back(makePlugin("b.jellyfin", "Jellyfin", PluginKind::ExternalHelper,
        {"remote.source", "remote.browse", "remote.search", "remote.stream"},
        {PluginDependency{/*plugin_id=*/std::nullopt, /*capability=*/"remote.auth.profile", /*required=*/true}}));

    auto graph = buildDependencyGraph(discovered);
    std::vector<std::string> warnings;
    auto order = topologicalSort(graph, warnings);

    // All 3 plugins in order, none skipped
    bool ok = (order.size() == 3);
    ok = ok && graph.skipped.empty();

    // Verify that C (index 1) comes before B (index 2) in order
    // because B depends on C's capability
    auto pos_c = std::find(order.begin(), order.end(), 1);
    auto pos_b = std::find(order.begin(), order.end(), 2);
    ok = ok && (pos_c != order.end()) && (pos_b != order.end());
    ok = ok && (pos_c < pos_b);  // C must come before B

    // --- Circular dependency test ---
    discovered.clear();
    discovered.push_back(makePlugin("x.first", "First", PluginKind::ExternalHelper,
        {"remote.source"}, {PluginDependency{std::nullopt, "remote.browse", true}}));
    discovered.push_back(makePlugin("y.second", "Second", PluginKind::ExternalHelper,
        {"remote.browse"}, {PluginDependency{std::nullopt, "remote.stream", true}}));
    discovered.push_back(makePlugin("z.third", "Third", PluginKind::ExternalHelper,
        {"remote.stream"}, {PluginDependency{std::nullopt, "remote.source", true}}));

    auto graph2 = buildDependencyGraph(discovered);
    std::vector<std::string> warnings2;
    auto order2 = topologicalSort(graph2, warnings2);

    // Circular: x -> y -> z -> x. All three should be detected as cycle and skipped.
    ok = ok && !warnings2.empty();

    // --- Optional dependency test ---
    discovered.clear();
    discovered.push_back(makePlugin("opt.main", "Main", PluginKind::ExternalHelper,
        {"remote.source"}, {PluginDependency{std::nullopt, "metadata.lyrics", false}}));

    auto graph3 = buildDependencyGraph(discovered);
    std::vector<std::string> warnings3;
    auto order3 = topologicalSort(graph3, warnings3);

    // Optional dependency not satisfied: plugin still loads.
    ok = ok && (order3.size() == 1) && graph3.skipped.empty();

    if (ok) {
        std::cerr << "pluginDiscoverySelfTest: PASSED\n";
    } else {
        std::cerr << "pluginDiscoverySelfTest: FAILED\n";
    }
    return ok;
}

} // namespace lofibox::plugins
