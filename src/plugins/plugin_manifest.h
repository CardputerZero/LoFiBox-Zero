// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <variant>
#include <vector>

namespace lofibox::plugins {

// --- Plugin kind ---

enum class PluginKind {
    InternalProvider,
    AssetPack,
    ExternalHelper,
    BinaryProvider,   // reserved, not used in Phase 1-2
};

[[nodiscard]] std::optional<PluginKind> pluginKindFromString(std::string_view s) noexcept;
[[nodiscard]] std::string_view pluginKindToString(PluginKind kind) noexcept;

// --- Entry (external_helper) ---

struct PluginEntry {
    std::string command{};
    std::vector<std::string> args{};
    std::string protocol{"lofibox-jsonrpc-1"};
    std::optional<std::string> cwd{};
    std::unordered_map<std::string, std::string> env{};
};

// --- Dependency declaration ---

struct PluginDependency {
    std::optional<std::string> plugin_id{};
    std::optional<std::string> capability{};
    bool required{true};
};

// --- Effect node (DSP) ---

struct EffectNode {
    std::string type{};
    std::unordered_map<std::string, std::variant<double, int, std::string>> params{};
};

struct EffectConfig {
    std::string type{"dsp_chain"};
    std::vector<EffectNode> nodes{};
};

// --- Manifest ---

struct PluginManifest {
    int schema_version{1};
    std::string id{};
    std::string name{};
    std::string version{};
    std::string api_version{"1"};
    PluginKind kind{PluginKind::InternalProvider};

    std::vector<std::string> capabilities{};

    // Optional metadata
    std::string description{};
    std::string author{};
    std::string homepage{};
    std::string license_spdx{};

    // Kind-specific
    std::optional<PluginEntry> entry{};
    std::vector<std::string> runtime_dependencies{};
    std::vector<std::string> permissions{};
    std::unordered_map<std::string, std::string> resources{};

    // Lifecycle
    bool hidden{false};
    bool builtin{false};
    std::vector<PluginDependency> depends_on{};

    // Type-specific config
    std::optional<EffectConfig> effect_config{};
    std::optional<std::string> skin_config_path{};

    // Source directory (set by discovery, not from JSON)
    std::filesystem::path source_dir{};
};

[[nodiscard]] bool manifestHasCapability(const PluginManifest& m, std::string_view cap) noexcept;
[[nodiscard]] bool manifestProvidesCapability(const PluginManifest& m, std::string_view cap) noexcept;

// --- Discovery result ---

enum class ManifestLoadStatus {
    Ok,
    ManifestMissing,
    ParseError,
    SchemaVersionUnsupported,
    FieldMissing,
    FieldInvalid,
};

struct DiscoveredPlugin {
    PluginManifest manifest{};
    ManifestLoadStatus status{ManifestLoadStatus::Ok};
    std::string error_message{};
};

// --- Dependency graph ---

struct DependencyEdge {
    std::size_t from_index{};
    std::size_t to_index{};
    bool required{true};
    std::string label{};   // plugin_id or capability for diagnostics
};

struct DependencyGraph {
    std::vector<DiscoveredPlugin> plugins{};
    std::vector<DependencyEdge> edges{};
    std::vector<std::size_t> skipped{};    // indices of plugins skipped due to errors
};

// --- Registry ---

class PluginRegistry {
public:
    void registerPlugin(PluginManifest manifest);
    [[nodiscard]] const std::vector<PluginManifest>& manifests() const noexcept;
    [[nodiscard]] const PluginManifest* findById(std::string_view id) const noexcept;
    [[nodiscard]] std::vector<const PluginManifest*> findByCapability(std::string_view capability) const noexcept;
    [[nodiscard]] std::vector<const PluginManifest*> findByKind(PluginKind kind) const noexcept;

    // Discovery: scan directories, parse manifests, resolve dependencies, sort.
    // Returns the load status for every discovered plugin.
    [[nodiscard]] std::vector<DiscoveredPlugin> discover(
        const std::vector<std::filesystem::path>& search_dirs);

    [[nodiscard]] const std::vector<std::string>& warnings() const noexcept { return warnings_; }

private:
    void clear();
    [[nodiscard]] std::vector<DiscoveredPlugin> loadAllManifests(
        const std::vector<std::filesystem::path>& search_dirs) const;
    [[nodiscard]] DependencyGraph buildDependencyGraph(
        std::vector<DiscoveredPlugin> discovered) const;
    [[nodiscard]] std::vector<std::size_t> topologicalSort(
        const DependencyGraph& graph);
    void activateInOrder(const std::vector<std::size_t>& order,
                         const DependencyGraph& graph);

    std::vector<PluginManifest> manifests_{};
    std::vector<std::string> warnings_{};
};

// --- Capability helpers ---

[[nodiscard]] bool isRecognizedCapability(std::string_view cap) noexcept;

} // namespace lofibox::plugins
