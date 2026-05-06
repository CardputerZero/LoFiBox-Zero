// SPDX-License-Identifier: GPL-3.0-or-later

#include "plugins/plugin_discovery.h"
#include "plugins/plugin_manifest.h"

#include <algorithm>
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

std::string manifestJson(
    const std::string& id,
    const std::string& version,
    const std::string& kind,
    const std::string& capabilities,
    const std::string& extra = {})
{
    return "{"
        "\"schema_version\":1,"
        "\"id\":\"" + id + "\","
        "\"name\":\"" + id + "\","
        "\"version\":\"" + version + "\","
        "\"api_version\":\"1\","
        "\"kind\":\"" + kind + "\","
        "\"capabilities\":" + capabilities +
        extra +
        "}";
}

bool writeText(const fs::path& path, const std::string& text)
{
    std::error_code ec{};
    fs::create_directories(path.parent_path(), ec);
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out << text;
    return out.good();
}

bool containsSkipped(const lofibox::plugins::DependencyGraph& graph, std::size_t index)
{
    return std::find(graph.skipped.begin(), graph.skipped.end(), index) != graph.skipped.end();
}

void test_manifest_validation()
{
    using namespace lofibox::plugins;

    const auto valid = parseManifestFromString(manifestJson(
        "io.github.vicliu624.lofibox.remote.test",
        "1.2.3",
        "external_helper",
        R"(["remote.source"])",
        R"(,"entry":{"command":"python3","args":["provider.py"],"protocol":"lofibox-jsonrpc-1","env":{"LOFIBOX_TEST":"1"}})"));
    assert(valid.status == ManifestLoadStatus::Ok);
    assert(valid.manifest.entry);
    assert(valid.manifest.entry->env.at("LOFIBOX_TEST") == "1");

    const auto bad_id = parseManifestFromString(manifestJson(
        "LoFiBox.Bad",
        "1.0.0",
        "asset_pack",
        R"(["ui.theme"])",
        R"(,"resources":{"skin":"skin.json"})"));
    assert(bad_id.status == ManifestLoadStatus::FieldInvalid);

    const auto bad_semver = parseManifestFromString(manifestJson(
        "io.github.vicliu624.lofibox.badversion",
        "1",
        "asset_pack",
        R"(["ui.theme"])",
        R"(,"resources":{"skin":"skin.json"})"));
    assert(bad_semver.status == ManifestLoadStatus::FieldInvalid);

    const auto unknown_caps = parseManifestFromString(manifestJson(
        "io.github.vicliu624.lofibox.unknowncaps",
        "1.0.0",
        "asset_pack",
        R"(["example.unknown"])",
        R"(,"resources":{"skin":"skin.json"})"));
    assert(unknown_caps.status == ManifestLoadStatus::FieldInvalid);
}

void test_dependency_graph_order_and_skips()
{
    using namespace lofibox::plugins;

    auto provider = parseManifestFromString(manifestJson(
        "io.github.vicliu624.lofibox.remote.provider",
        "1.0.0",
        "external_helper",
        R"(["remote.source"])",
        R"(,"entry":{"command":"python3","args":["provider.py"],"protocol":"lofibox-jsonrpc-1"},"depends_on":[{"plugin_id":"io.github.vicliu624.lofibox.remote.auth","required":true}])"));
    auto auth = parseManifestFromString(manifestJson(
        "io.github.vicliu624.lofibox.remote.auth",
        "1.0.0",
        "external_helper",
        R"(["remote.auth.profile"])",
        R"(,"entry":{"command":"python3","args":["auth.py"],"protocol":"lofibox-jsonrpc-1"})"));
    auto graph = buildDependencyGraph({provider, auth});
    std::vector<std::string> warnings;
    auto order = topologicalSort(graph, warnings);
    assert(order.size() == 2);
    assert(graph.plugins[order[0]].manifest.id == "io.github.vicliu624.lofibox.remote.auth");
    assert(graph.plugins[order[1]].manifest.id == "io.github.vicliu624.lofibox.remote.provider");
    assert(warnings.empty());

    auto missing = parseManifestFromString(manifestJson(
        "io.github.vicliu624.lofibox.remote.needsmissing",
        "1.0.0",
        "external_helper",
        R"(["remote.source"])",
        R"(,"entry":{"command":"python3","args":["provider.py"],"protocol":"lofibox-jsonrpc-1"},"depends_on":[{"plugin_id":"io.github.vicliu624.lofibox.remote.missing","required":true}])"));
    auto missing_graph = buildDependencyGraph({missing});
    auto missing_order = topologicalSort(missing_graph, warnings);
    assert(missing_order.empty());
    assert(containsSkipped(missing_graph, 0));
}

void test_registry_keeps_valid_after_invalid()
{
    using namespace lofibox::plugins;

    const auto root = fs::temp_directory_path() / "lofibox-plugin-discovery-smoke";
    std::error_code ec{};
    fs::remove_all(root, ec);

    assert(writeText(root / "invalid" / "plugin.json", manifestJson(
        "Bad.Plugin",
        "1.0.0",
        "asset_pack",
        R"(["ui.theme"])",
        R"(,"resources":{"skin":"skin.json"})")));
    assert(writeText(root / "valid" / "plugin.json", manifestJson(
        "io.github.vicliu624.lofibox.theme.valid",
        "1.0.0",
        "asset_pack",
        R"(["ui.theme"])",
        R"(,"resources":{"skin":"skin.json"})")));

    PluginRegistry registry;
    const auto discovered = registry.discover({root});
    assert(discovered.size() == 2);
    assert(registry.manifests().size() == 1);
    assert(registry.findById("io.github.vicliu624.lofibox.theme.valid") != nullptr);

    fs::remove_all(root, ec);
}

} // namespace

int main()
{
    test_manifest_validation();
    test_dependency_graph_order_and_skips();
    test_registry_keeps_valid_after_invalid();
    std::cout << "plugin discovery smoke passed\n";
    return 0;
}
