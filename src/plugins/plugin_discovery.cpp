// SPDX-License-Identifier: GPL-3.0-or-later

#include "plugins/plugin_discovery.h"

#include <algorithm>
#include <cctype>
#include <deque>
#include <fstream>
#include <optional>
#include <queue>
#include <set>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

namespace lofibox::plugins {
namespace {

// ---------------------------------------------------------------------------
// Lightweight JSON helpers (no library dependency)
// ---------------------------------------------------------------------------

std::string readFile(const std::filesystem::path& path)
{
    std::ifstream file(path, std::ios::in | std::ios::binary);
    if (!file.is_open()) return {};
    std::stringstream buf;
    buf << file.rdbuf();
    return buf.str();
}

void skipWhitespace(std::string_view json, std::size_t& pos)
{
    while (pos < json.size() &&
           (json[pos] == ' ' || json[pos] == '\t' ||
            json[pos] == '\n' || json[pos] == '\r')) {
        ++pos;
    }
}

std::string extractString(std::string_view json, std::size_t& pos)
{
    if (pos >= json.size() || json[pos] != '"') return {};
    ++pos;
    std::string out;
    while (pos < json.size() && json[pos] != '"') {
        if (json[pos] == '\\' && pos + 1 < json.size()) {
            ++pos;
            switch (json[pos]) {
            case '"':  out += '"';  break;
            case '\\': out += '\\'; break;
            case 'n':  out += '\n'; break;
            case 't':  out += '\t'; break;
            default:   out += json[pos]; break;
            }
            ++pos;
        } else {
            out += json[pos++];
        }
    }
    if (pos < json.size()) ++pos; // closing quote
    return out;
}

int extractInt(std::string_view json, std::size_t& pos)
{
    skipWhitespace(json, pos);
    int val = 0;
    bool neg = false;
    if (pos < json.size() && json[pos] == '-') { neg = true; ++pos; }
    while (pos < json.size() && std::isdigit(static_cast<unsigned char>(json[pos]))) {
        val = val * 10 + (json[pos] - '0');
        ++pos;
    }
    return neg ? -val : val;
}

bool extractBool(std::string_view json, std::size_t& pos)
{
    skipWhitespace(json, pos);
    if (pos + 4 <= json.size() && json.substr(pos, 4) == "true")  { pos += 4; return true; }
    if (pos + 5 <= json.size() && json.substr(pos, 5) == "false") { pos += 5; return false; }
    return false;
}

void skipValue(std::string_view json, std::size_t& pos)
{
    skipWhitespace(json, pos);
    if (pos >= json.size()) return;
    if (json[pos] == '"') { extractString(json, pos); return; }
    if (json[pos] == '{') {
        int d = 1; ++pos;
        while (pos < json.size() && d > 0) {
            if (json[pos] == '"') extractString(json, pos);
            else { if (json[pos] == '{') ++d; else if (json[pos] == '}') --d; ++pos; }
        }
        return;
    }
    if (json[pos] == '[') {
        int d = 1; ++pos;
        while (pos < json.size() && d > 0) {
            if (json[pos] == '"') extractString(json, pos);
            else { if (json[pos] == '[') ++d; else if (json[pos] == ']') --d; ++pos; }
        }
        return;
    }
    while (pos < json.size() && json[pos] != ',' && json[pos] != '}' && json[pos] != ']') ++pos;
}

std::string fieldString(std::string_view json, std::string_view name)
{
    auto search = "\"" + std::string(name) + "\"";
    auto p = json.find(search);
    if (p == std::string_view::npos) return {};
    p += search.size();
    skipWhitespace(json, p);
    if (p < json.size() && json[p] == ':') { ++p; skipWhitespace(json, p); return extractString(json, p); }
    return {};
}

std::optional<int> fieldInt(std::string_view json, std::string_view name)
{
    auto search = "\"" + std::string(name) + "\"";
    auto p = json.find(search);
    if (p == std::string_view::npos) return std::nullopt;
    p += search.size();
    skipWhitespace(json, p);
    if (p < json.size() && json[p] == ':') { ++p; return extractInt(json, p); }
    return std::nullopt;
}

std::optional<bool> fieldBool(std::string_view json, std::string_view name)
{
    auto search = "\"" + std::string(name) + "\"";
    auto p = json.find(search);
    if (p == std::string_view::npos) return std::nullopt;
    p += search.size();
    skipWhitespace(json, p);
    if (p < json.size() && json[p] == ':') { ++p; return extractBool(json, p); }
    return std::nullopt;
}

// Extract a string array: "name": ["a", "b"]
std::vector<std::string> fieldStringArray(std::string_view json, std::string_view name)
{
    std::vector<std::string> result;
    auto search = "\"" + std::string(name) + "\"";
    auto p = json.find(search);
    if (p == std::string_view::npos) return result;
    p += search.size();
    skipWhitespace(json, p);
    if (p >= json.size() || json[p] != ':') return result;
    ++p;
    skipWhitespace(json, p);
    if (p >= json.size() || json[p] != '[') return result;
    ++p;
    while (p < json.size()) {
        skipWhitespace(json, p);
        if (p >= json.size()) break;
        if (json[p] == ']') break;
        if (json[p] == ',') { ++p; continue; }
        result.push_back(extractString(json, p));
    }
    return result;
}

// Extract an object field as a raw JSON substring
std::optional<std::string> fieldObjectRaw(std::string_view json, std::string_view name)
{
    auto search = "\"" + std::string(name) + "\"";
    auto p = json.find(search);
    if (p == std::string_view::npos) return std::nullopt;
    p += search.size();
    skipWhitespace(json, p);
    if (p >= json.size() || json[p] != ':') return std::nullopt;
    ++p;
    skipWhitespace(json, p);
    if (p >= json.size() || json[p] != '{') return std::nullopt;
    auto start = p;
    int depth = 1; ++p;
    while (p < json.size() && depth > 0) {
        if (json[p] == '"') { extractString(json, p); continue; }
        if (json[p] == '{') ++depth;
        else if (json[p] == '}') --depth;
        ++p;
    }
    return std::string(json.substr(start, p - start));
}

// Extract an array of objects: "name": [{...}, {...}]
std::vector<std::string> fieldObjectArray(std::string_view json, std::string_view name)
{
    std::vector<std::string> result;
    auto search = "\"" + std::string(name) + "\"";
    auto p = json.find(search);
    if (p == std::string_view::npos) return result;
    p += search.size();
    skipWhitespace(json, p);
    if (p >= json.size() || json[p] != ':') return result;
    ++p;
    skipWhitespace(json, p);
    if (p >= json.size() || json[p] != '[') return result;
    ++p;
    while (p < json.size()) {
        skipWhitespace(json, p);
        if (p >= json.size() || json[p] == ']') break;
        if (json[p] == ',') { ++p; continue; }
        if (json[p] == '{') {
            auto start = p;
            int depth = 1; ++p;
            while (p < json.size() && depth > 0) {
                if (json[p] == '"') { extractString(json, p); continue; }
                if (json[p] == '{') ++depth;
                else if (json[p] == '}') --depth;
                ++p;
            }
            result.push_back(std::string(json.substr(start, p - start)));
        } else {
            ++p;
        }
    }
    return result;
}

std::unordered_map<std::string, std::string> fieldStringMap(std::string_view json, std::string_view name)
{
    std::unordered_map<std::string, std::string> map;
    auto raw = fieldObjectRaw(json, name);
    if (!raw) return map;

    std::size_t p = 1;
    while (p < raw->size()) {
        skipWhitespace(*raw, p);
        if (p >= raw->size() || (*raw)[p] == '}') break;
        auto key = extractString(*raw, p);
        skipWhitespace(*raw, p);
        if (p < raw->size() && (*raw)[p] == ':') {
            ++p;
            skipWhitespace(*raw, p);
            auto val = extractString(*raw, p);
            if (!key.empty()) {
                map[key] = val;
            }
        }
        skipWhitespace(*raw, p);
        if (p < raw->size() && (*raw)[p] == ',') ++p;
    }
    return map;
}

// --- Entry parsing ---

std::optional<PluginEntry> parseEntry(std::string_view entry_json)
{
    PluginEntry entry;
    entry.command = fieldString(entry_json, "command");
    if (entry.command.empty()) return std::nullopt;

    entry.args = fieldStringArray(entry_json, "args");
    entry.protocol = fieldString(entry_json, "protocol");
    if (entry.protocol.empty()) entry.protocol = "lofibox-jsonrpc-1";
    if (auto cwd = fieldString(entry_json, "cwd"); !cwd.empty()) entry.cwd = cwd;
    entry.env = fieldStringMap(entry_json, "env");

    return entry;
}

bool validPluginId(std::string_view id)
{
    if (id.size() < 3 || id.size() > 128) return false;
    if (id.front() < 'a' || id.front() > 'z') return false;
    bool has_dot = false;
    char prev = '\0';
    for (char ch : id) {
        const bool ok = (ch >= 'a' && ch <= 'z')
            || (ch >= '0' && ch <= '9')
            || ch == '.'
            || ch == '_'
            || ch == '-';
        if (!ok) return false;
        if (ch == '.') {
            if (prev == '.') return false;
            has_dot = true;
        }
        prev = ch;
    }
    return has_dot && id.back() != '.';
}

bool validSemver(std::string_view version)
{
    int component = 0;
    int digits_in_component = 0;
    bool seen_suffix = false;
    bool suffix_has_char = false;
    for (char ch : version) {
        if (seen_suffix) {
            const bool ok = std::isalnum(static_cast<unsigned char>(ch)) || ch == '.' || ch == '-';
            if (!ok) return false;
            suffix_has_char = true;
            continue;
        }
        if (std::isdigit(static_cast<unsigned char>(ch))) {
            ++digits_in_component;
            continue;
        }
        if (ch == '.') {
            if (digits_in_component == 0 || component >= 2) return false;
            ++component;
            digits_in_component = 0;
            continue;
        }
        if (ch == '-') {
            if (component != 2 || digits_in_component == 0) return false;
            seen_suffix = true;
            continue;
        }
        return false;
    }
    return component == 2 && digits_in_component > 0 && (!seen_suffix || suffix_has_char);
}

bool hasRecognizedCapability(const std::vector<std::string>& capabilities)
{
    return std::any_of(capabilities.begin(), capabilities.end(), [](const std::string& cap) {
        return isRecognizedCapability(cap);
    });
}

// --- Dependency parsing ---

std::vector<PluginDependency> parseDependsOn(std::string_view json)
{
    std::vector<PluginDependency> deps;
    auto blocks = fieldObjectArray(json, "depends_on");
    for (const auto& block : blocks) {
        PluginDependency dep;
        dep.plugin_id = fieldString(block, "plugin_id");
        dep.capability = fieldString(block, "capability");
        if (auto req = fieldBool(block, "required")) dep.required = *req;
        if (dep.plugin_id || dep.capability) {
            // Normalize empty strings to nullopt
            if (dep.plugin_id && dep.plugin_id->empty()) dep.plugin_id.reset();
            if (dep.capability && dep.capability->empty()) dep.capability.reset();
            deps.push_back(dep);
        }
    }
    return deps;
}

// --- Effect config parsing ---

std::optional<EffectConfig> parseEffectConfig(std::string_view config_json)
{
    EffectConfig cfg;
    cfg.type = fieldString(config_json, "type");
    if (cfg.type.empty()) cfg.type = "dsp_chain";

    auto nodes_raw = fieldObjectArray(config_json, "nodes");
    for (const auto& node_json : nodes_raw) {
        EffectNode node;
        node.type = fieldString(node_json, "type");
        if (node.type.empty()) continue;
        cfg.nodes.push_back(std::move(node));
    }
    if (cfg.nodes.empty()) return std::nullopt;
    return cfg;
}

// --- Resource map parsing ---

std::unordered_map<std::string, std::string> parseResources(std::string_view json)
{
    return fieldStringMap(json, "resources");
}

} // namespace

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

std::optional<PluginKind> pluginKindFromString(std::string_view s) noexcept
{
    if (s == "internal_provider") return PluginKind::InternalProvider;
    if (s == "asset_pack")        return PluginKind::AssetPack;
    if (s == "external_helper")   return PluginKind::ExternalHelper;
    if (s == "binary_provider")   return PluginKind::BinaryProvider;
    return std::nullopt;
}

std::string_view pluginKindToString(PluginKind kind) noexcept
{
    switch (kind) {
    case PluginKind::InternalProvider: return "internal_provider";
    case PluginKind::AssetPack:        return "asset_pack";
    case PluginKind::ExternalHelper:   return "external_helper";
    case PluginKind::BinaryProvider:   return "binary_provider";
    }
    return "internal_provider";
}

bool manifestHasCapability(const PluginManifest& m, std::string_view cap) noexcept
{
    return std::find(m.capabilities.begin(), m.capabilities.end(), cap) != m.capabilities.end();
}

bool manifestProvidesCapability(const PluginManifest& m, std::string_view cap) noexcept
{
    // A plugin "provides" a capability if it declares it and is valid.
    // Non-OK manifests don't provide anything.
    return manifestHasCapability(m, cap);
}

bool isRecognizedCapability(std::string_view cap) noexcept
{
    static const std::set<std::string_view> recognized = {
        "ui.theme", "ui.icons",
        "ui.layout.now_playing", "ui.layout.library", "ui.layout.eq", "ui.layout.lyrics",
        "audio.effect", "audio.effect.realtime", "audio.effect.offline", "audio.effect.visualizer",
        "remote.source", "remote.browse", "remote.search", "remote.stream", "remote.auth.profile",
        "metadata.reader", "metadata.enricher", "metadata.lyrics", "metadata.artwork",
        "metadata.fingerprint", "metadata.tag_writer",
    };
    return recognized.count(cap) > 0;
}

DiscoveredPlugin parseManifestFromString(std::string_view json, std::string_view source_hint)
{
    DiscoveredPlugin result;

    // --- Required: schema_version ---
    auto sv = fieldInt(json, "schema_version");
    if (!sv) {
        result.status = ManifestLoadStatus::FieldMissing;
        result.error_message = std::string(source_hint) + ": missing required field 'schema_version'";
        return result;
    }
    if (*sv != 1) {
        result.status = ManifestLoadStatus::SchemaVersionUnsupported;
        result.error_message = std::string(source_hint) + ": unsupported schema_version " + std::to_string(*sv);
        return result;
    }
    result.manifest.schema_version = *sv;

    // --- Required: id ---
    result.manifest.id = fieldString(json, "id");
    if (result.manifest.id.empty()) {
        result.status = ManifestLoadStatus::FieldMissing;
        result.error_message = std::string(source_hint) + ": missing required field 'id'";
        return result;
    }
    if (!validPluginId(result.manifest.id)) {
        result.status = ManifestLoadStatus::FieldInvalid;
        result.error_message = std::string(source_hint) + ": invalid plugin id '" + result.manifest.id + "'";
        return result;
    }

    // --- Required: name ---
    result.manifest.name = fieldString(json, "name");
    if (result.manifest.name.empty()) {
        result.status = ManifestLoadStatus::FieldMissing;
        result.error_message = std::string(source_hint) + ": missing required field 'name'";
        return result;
    }

    // --- Required: version ---
    result.manifest.version = fieldString(json, "version");
    if (result.manifest.version.empty()) {
        result.status = ManifestLoadStatus::FieldMissing;
        result.error_message = std::string(source_hint) + ": missing required field 'version'";
        return result;
    }
    if (!validSemver(result.manifest.version)) {
        result.status = ManifestLoadStatus::FieldInvalid;
        result.error_message = std::string(source_hint) + ": invalid semver '" + result.manifest.version + "'";
        return result;
    }

    // --- Required: api_version ---
    if (auto v = fieldString(json, "api_version"); !v.empty()) result.manifest.api_version = v;
    if (result.manifest.api_version != "1") {
        result.status = ManifestLoadStatus::FieldInvalid;
        result.error_message = std::string(source_hint) + ": unsupported api_version '" + result.manifest.api_version + "'";
        return result;
    }

    // --- Required: kind ---
    auto kind_str = fieldString(json, "kind");
    auto kind = pluginKindFromString(kind_str);
    if (!kind) {
        result.status = ManifestLoadStatus::FieldInvalid;
        result.error_message = std::string(source_hint) + ": invalid or missing 'kind': '" + kind_str + "'";
        return result;
    }
    if (*kind == PluginKind::BinaryProvider) {
        result.status = ManifestLoadStatus::FieldInvalid;
        result.error_message = std::string(source_hint) + ": binary_provider is reserved and cannot be loaded";
        return result;
    }
    result.manifest.kind = *kind;

    // --- Required: capabilities ---
    result.manifest.capabilities = fieldStringArray(json, "capabilities");
    if (result.manifest.capabilities.empty()) {
        result.status = ManifestLoadStatus::FieldMissing;
        result.error_message = std::string(source_hint) + ": missing required field 'capabilities'";
        return result;
    }
    if (!hasRecognizedCapability(result.manifest.capabilities)) {
        result.status = ManifestLoadStatus::FieldInvalid;
        result.error_message = std::string(source_hint) + ": capabilities contain no recognized extension point";
        return result;
    }

    // --- Kind-specific: entry (external_helper) ---
    if (*kind == PluginKind::ExternalHelper) {
        auto entry_raw = fieldObjectRaw(json, "entry");
        if (!entry_raw) {
            result.status = ManifestLoadStatus::FieldMissing;
            result.error_message = std::string(source_hint) + ": external_helper requires 'entry' field";
            return result;
        }
        auto entry = parseEntry(*entry_raw);
        if (!entry || entry->command.empty()) {
            result.status = ManifestLoadStatus::FieldMissing;
            result.error_message = std::string(source_hint) + ": entry.command is required";
            return result;
        }
        if (entry->protocol != "lofibox-jsonrpc-1") {
            result.status = ManifestLoadStatus::FieldInvalid;
            result.error_message = std::string(source_hint) + ": unsupported entry.protocol '" + entry->protocol + "'";
            return result;
        }
        result.manifest.entry = std::move(entry);
    }

    // --- Optional fields ---
    result.manifest.description = fieldString(json, "description");
    result.manifest.author = fieldString(json, "author");
    result.manifest.homepage = fieldString(json, "homepage");
    result.manifest.license_spdx = fieldString(json, "license");

    if (auto v = fieldBool(json, "hidden")) result.manifest.hidden = *v;
    if (auto v = fieldBool(json, "builtin")) result.manifest.builtin = *v;

    result.manifest.runtime_dependencies = fieldStringArray(json, "runtime_dependencies");
    result.manifest.permissions = fieldStringArray(json, "permissions");
    result.manifest.resources = parseResources(json);
    if (*kind == PluginKind::AssetPack && result.manifest.resources.empty()) {
        result.status = ManifestLoadStatus::FieldMissing;
        result.error_message = std::string(source_hint) + ": asset_pack requires at least one resource path";
        return result;
    }

    // Depends_on
    result.manifest.depends_on = parseDependsOn(json);

    // Config (kind-specific)
    if (*kind == PluginKind::InternalProvider) {
        auto config_raw = fieldObjectRaw(json, "config");
        if (config_raw) {
            auto effect_raw = fieldObjectRaw(*config_raw, "effect");
            if (effect_raw) result.manifest.effect_config = parseEffectConfig(*effect_raw);
        }
    }
    if (*kind == PluginKind::AssetPack) {
        auto config_raw = fieldObjectRaw(json, "config");
        if (config_raw) {
            auto skin = fieldString(*config_raw, "skin");
            if (!skin.empty()) result.manifest.skin_config_path = skin;
        }
    }

    result.status = ManifestLoadStatus::Ok;
    return result;
}

DiscoveredPlugin parseManifestFromFile(const std::filesystem::path& json_path)
{
    auto content = readFile(json_path);
    if (content.empty()) {
        DiscoveredPlugin fail;
        fail.status = ManifestLoadStatus::ManifestMissing;
        fail.error_message = json_path.string() + ": file not found or empty";
        return fail;
    }
    auto result = parseManifestFromString(content, json_path.string());
    if (result.status == ManifestLoadStatus::Ok) {
        result.manifest.source_dir = json_path.parent_path();
    }
    return result;
}

std::vector<DiscoveredPlugin> scanPluginDirectory(const std::filesystem::path& dir)
{
    std::vector<DiscoveredPlugin> results;
    if (!std::filesystem::exists(dir)) return results;

    for (const auto& entry : std::filesystem::directory_iterator(dir)) {
        if (!entry.is_directory()) continue;
        auto manifest_path = entry.path() / "plugin.json";
        if (!std::filesystem::exists(manifest_path)) continue;
        results.push_back(parseManifestFromFile(manifest_path));
    }
    return results;
}

// ---------------------------------------------------------------------------
// Dependency graph
// ---------------------------------------------------------------------------

std::optional<std::size_t> resolveDependency(
    const PluginDependency& dep,
    const std::vector<DiscoveredPlugin>& plugins)
{
    // Try exact plugin_id match first
    if (dep.plugin_id && !dep.plugin_id->empty()) {
        for (std::size_t i = 0; i < plugins.size(); ++i) {
            if (plugins[i].status == ManifestLoadStatus::Ok &&
                plugins[i].manifest.id == *dep.plugin_id) {
                return i;
            }
        }
    }

    // Try capability match
    if (dep.capability && !dep.capability->empty()) {
        for (std::size_t i = 0; i < plugins.size(); ++i) {
            if (plugins[i].status == ManifestLoadStatus::Ok &&
                manifestHasCapability(plugins[i].manifest, *dep.capability)) {
                return i;
            }
        }
    }

    return std::nullopt;
}

DependencyGraph buildDependencyGraph(
    const std::vector<DiscoveredPlugin>& discovered)
{
    DependencyGraph graph;
    graph.plugins = discovered;

    const auto mark_skipped = [&](std::size_t index) {
        if (std::find(graph.skipped.begin(), graph.skipped.end(), index) == graph.skipped.end()) {
            graph.skipped.push_back(index);
        }
    };

    for (std::size_t i = 0; i < graph.plugins.size(); ++i) {
        if (graph.plugins[i].status != ManifestLoadStatus::Ok) {
            mark_skipped(i);
        }
    }

    // Build edges from depends_on
    for (std::size_t i = 0; i < graph.plugins.size(); ++i) {
        if (graph.plugins[i].status != ManifestLoadStatus::Ok) continue;
        for (const auto& dep : graph.plugins[i].manifest.depends_on) {
            auto target = resolveDependency(dep, graph.plugins);
            if (target) {
                graph.edges.push_back(DependencyEdge{
                    .from_index = i,
                    .to_index = *target,
                    .required = dep.required,
                    .label = dep.plugin_id.value_or(dep.capability.value_or("unknown")),
                });
            } else if (dep.required) {
                graph.plugins[i].status = ManifestLoadStatus::FieldInvalid;
                graph.plugins[i].error_message = "Required dependency not satisfied: " +
                    (dep.plugin_id ? "plugin_id=" + *dep.plugin_id : "capability=" + dep.capability.value_or("unknown"));
                mark_skipped(i);
            }
            // Optional unsatisfied: skip the edge and keep the plugin loadable.
        }
    }

    return graph;
}

std::vector<std::size_t> topologicalSort(
    const DependencyGraph& graph,
    std::vector<std::string>& warnings)
{
    const auto n = graph.plugins.size();

    // Build adjacency list and in-degree count
    std::vector<std::vector<std::size_t>> adj(n);
    std::vector<int> in_degree(n, 0);

    for (const auto& edge : graph.edges) {
        // Skip edges involving skipped plugins
        bool from_skipped = std::find(graph.skipped.begin(), graph.skipped.end(), edge.from_index) != graph.skipped.end();
        bool to_skipped = std::find(graph.skipped.begin(), graph.skipped.end(), edge.to_index) != graph.skipped.end();
        if (from_skipped || to_skipped) continue;

        adj[edge.to_index].push_back(edge.from_index); // from must load before to.
        in_degree[edge.from_index]++;
    }

    // Kahn's algorithm
    std::queue<std::size_t> queue;
    for (std::size_t i = 0; i < n; ++i) {
        bool skipped = std::find(graph.skipped.begin(), graph.skipped.end(), i) != graph.skipped.end();
        if (!skipped && in_degree[i] == 0) {
            queue.push(i);
        }
    }

    std::vector<std::size_t> order;
    while (!queue.empty()) {
        auto u = queue.front();
        queue.pop();
        order.push_back(u);

        for (auto v : adj[u]) {
            if (--in_degree[v] == 0) {
                queue.push(v);
            }
        }
    }

    // Detect cycles: nodes that never reached in_degree 0
    for (std::size_t i = 0; i < n; ++i) {
        bool skipped = std::find(graph.skipped.begin(), graph.skipped.end(), i) != graph.skipped.end();
        if (!skipped && in_degree[i] > 0) {
            warnings.push_back("Circular dependency detected involving plugin '" +
                               graph.plugins[i].manifest.id + "'. Plugin disabled.");
        }
    }

    return order;
}

} // namespace lofibox::plugins
