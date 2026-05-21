// SPDX-License-Identifier: GPL-3.0-or-later

#include "platform/host/runtime_remote_media_tool.h"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "platform/host/runtime_host_internal.h"
#include "platform/host/runtime_logger.h"
#include "platform/host/runtime_paths.h"
#include "plugins/plugin_discovery.h"
#include "plugins/plugin_manifest.h"
#include "remote/common/stream_source_model.h"
#include "security/credential_policy.h"

namespace fs = std::filesystem;

namespace lofibox::platform::host::runtime_detail {
namespace {

std::string toKindString(app::RemoteServerKind kind)
{
    switch (kind) {
    case app::RemoteServerKind::LocalRoot: return "local-root";
    case app::RemoteServerKind::Jellyfin: return "jellyfin";
    case app::RemoteServerKind::OpenSubsonic: return "opensubsonic";
    case app::RemoteServerKind::Navidrome: return "navidrome";
    case app::RemoteServerKind::Emby: return "emby";
    case app::RemoteServerKind::DirectUrl: return "direct-url";
    case app::RemoteServerKind::InternetRadio: return "internet-radio";
    case app::RemoteServerKind::PlaylistManifest: return "playlist-manifest";
    case app::RemoteServerKind::Hls: return "hls";
    case app::RemoteServerKind::Dash: return "dash";
    case app::RemoteServerKind::Smb: return "smb";
    case app::RemoteServerKind::Nfs: return "nfs";
    case app::RemoteServerKind::WebDav: return "webdav";
    case app::RemoteServerKind::Ftp: return "ftp";
    case app::RemoteServerKind::Sftp: return "sftp";
    case app::RemoteServerKind::DlnaUpnp: return "dlna-upnp";
    }
    return "jellyfin";
}

bool handledLocally(app::RemoteServerKind kind) noexcept
{
    switch (kind) {
    case app::RemoteServerKind::DirectUrl:
    case app::RemoteServerKind::InternetRadio:
    case app::RemoteServerKind::Hls:
    case app::RemoteServerKind::Dash:
        return true;
    default:
        return false;
    }
}

std::vector<fs::path> pluginSearchDirs()
{
    std::vector<fs::path> dirs;
    if (const char* env = std::getenv("LOFIBOX_PLUGIN_DIR"); env != nullptr && *env != '\0') {
        dirs.emplace_back(env);
    }
    dirs.push_back(projectRoot() / "data" / "plugins");
    dirs.push_back(runtime_paths::appDataDir() / "plugins");
#if !defined(_WIN32)
    dirs.emplace_back("/usr/share/lofibox/plugins");
#endif
    return dirs;
}

std::string remotePluginShortName(std::string_view plugin_id)
{
    const auto dot = plugin_id.rfind('.');
    return dot == std::string_view::npos ? std::string{plugin_id} : std::string{plugin_id.substr(dot + 1)};
}

std::string pluginLogId(std::string plugin_id)
{
    std::replace(plugin_id.begin(), plugin_id.end(), '.', '_');
    std::replace(plugin_id.begin(), plugin_id.end(), '/', '_');
    return plugin_id;
}

bool legacyToolAvailable(const std::optional<fs::path>& python_path)
{
    return python_path.has_value() && fs::exists(remoteMediaToolScriptPath());
}

app::StreamProtocol protocolFromDirectEntry(const ::lofibox::remote::DirectStreamEntry& entry) noexcept
{
    return entry.protocol;
}

bool isLiveKind(app::RemoteServerKind kind) noexcept
{
    return kind == app::RemoteServerKind::InternetRadio;
}

app::RemoteTrack localRemoteTrack(const app::RemoteServerProfile& profile)
{
    app::RemoteTrack track{};
    track.id = profile.base_url;
    track.title = profile.name.empty() ? "REMOTE STREAM" : profile.name;
    track.source_id = profile.id;
    track.source_label = profile.name;
    return track;
}

app::RemoteCatalogNode localRemoteNode(const app::RemoteServerProfile& profile)
{
    app::RemoteCatalogNode node{};
    node.kind = app::RemoteCatalogNodeKind::Tracks;
    node.id = profile.base_url;
    node.title = profile.name.empty() ? "REMOTE STREAM" : profile.name;
    node.subtitle = profile.base_url;
    node.playable = true;
    node.browsable = false;
    node.source_id = profile.id;
    node.source_label = profile.name;
    return node;
}

std::string remoteProfileJson(const app::RemoteServerProfile& profile)
{
    std::ostringstream out;
    out << "{"
        << "\"kind\":\"" << toKindString(profile.kind) << "\","
        << "\"id\":\"" << jsonEscape(profile.id) << "\","
        << "\"name\":\"" << jsonEscape(profile.name) << "\","
        << "\"base_url\":\"" << jsonEscape(profile.base_url) << "\","
        << "\"username\":\"" << jsonEscape(profile.username) << "\","
        << "\"password\":\"" << jsonEscape(profile.password) << "\","
        << "\"api_token\":\"" << jsonEscape(profile.api_token) << "\""
        << "}";
    return out.str();
}

std::string remoteSessionJson(const app::RemoteSourceSession& session)
{
    std::ostringstream out;
    out << "{\"available\":" << (session.available ? "true" : "false")
        << ",\"server_name\":\"" << jsonEscape(session.server_name)
        << "\",\"user_id\":\"" << jsonEscape(session.user_id)
        << "\",\"access_token\":\"" << jsonEscape(session.access_token)
        << "\",\"message\":\"" << jsonEscape(session.message)
        << "\"}";
    return out.str();
}

app::RemoteSourceSession parseRemoteSession(std::string_view json)
{
    app::RemoteSourceSession session{};
    if (const auto value = extractJsonString(json, "\"server_name\":\"")) session.server_name = repairMetadataText(*value);
    if (const auto value = extractJsonString(json, "\"user_id\":\"")) session.user_id = *value;
    if (const auto value = extractJsonString(json, "\"access_token\":\"")) session.access_token = *value;
    if (const auto value = extractJsonString(json, "\"message\":\"")) session.message = repairMetadataText(*value);
    session.available = parseJsonBool(json, "\"available\":").value_or(false);
    return session;
}

std::vector<app::RemoteTrack> parseRemoteTracks(std::string_view json)
{
    std::vector<app::RemoteTrack> tracks{};
    std::size_t pos = json.find("\"tracks\":[");
    if (pos == std::string_view::npos) {
        return tracks;
    }

    pos = json.find('{', pos);
    while (pos != std::string_view::npos) {
        const auto end = json.find('}', pos);
        if (end == std::string_view::npos) {
            break;
        }
        const auto block = json.substr(pos, end - pos + 1);
        app::RemoteTrack track{};
        if (const auto value = extractJsonString(block, "\"id\":\"")) track.id = *value;
        if (const auto value = extractJsonString(block, "\"title\":\"")) track.title = repairMetadataText(*value);
        if (const auto value = extractJsonString(block, "\"artist\":\"")) track.artist = repairMetadataText(*value);
        if (const auto value = extractJsonString(block, "\"album\":\"")) track.album = repairMetadataText(*value);
        if (const auto value = extractJsonString(block, "\"album_id\":\"")) track.album_id = *value;
        if (const auto value = extractJsonString(block, "\"source_id\":\"")) track.source_id = *value;
        if (const auto value = extractJsonString(block, "\"source_label\":\"")) track.source_label = repairMetadataText(*value);
        if (const auto value = extractJsonString(block, "\"artwork_key\":\"")) track.artwork_key = *value;
        if (const auto value = extractJsonString(block, "\"artwork_url\":\"")) track.artwork_url = *value;
        if (const auto value = extractJsonString(block, "\"lyrics_plain\":\"")) track.lyrics_plain = repairMetadataText(*value);
        if (const auto value = extractJsonString(block, "\"lyrics_synced\":\"")) track.lyrics_synced = repairMetadataText(*value);
        if (const auto value = extractJsonString(block, "\"lyrics_source\":\"")) track.lyrics_source = repairMetadataText(*value);
        if (const auto value = extractJsonString(block, "\"fingerprint\":\"")) track.fingerprint = *value;
        if (const auto duration = parseJsonInt(block, "\"duration_seconds\":")) {
            track.duration_seconds = *duration;
        }
        if (!track.id.empty()) {
            tracks.push_back(track);
        }
        pos = json.find('{', end + 1);
    }
    return tracks;
}

app::RemoteCatalogNodeKind parseNodeKind(std::string_view value) noexcept
{
    if (value == "artists") return app::RemoteCatalogNodeKind::Artists;
    if (value == "artist") return app::RemoteCatalogNodeKind::Artist;
    if (value == "albums") return app::RemoteCatalogNodeKind::Albums;
    if (value == "album") return app::RemoteCatalogNodeKind::Album;
    if (value == "tracks") return app::RemoteCatalogNodeKind::Tracks;
    if (value == "genres") return app::RemoteCatalogNodeKind::Genres;
    if (value == "genre") return app::RemoteCatalogNodeKind::Genre;
    if (value == "playlists") return app::RemoteCatalogNodeKind::Playlists;
    if (value == "playlist") return app::RemoteCatalogNodeKind::Playlist;
    if (value == "folders") return app::RemoteCatalogNodeKind::Folders;
    if (value == "folder") return app::RemoteCatalogNodeKind::Folder;
    if (value == "favorites") return app::RemoteCatalogNodeKind::Favorites;
    if (value == "recently-added") return app::RemoteCatalogNodeKind::RecentlyAdded;
    if (value == "recently-played") return app::RemoteCatalogNodeKind::RecentlyPlayed;
    if (value == "stations") return app::RemoteCatalogNodeKind::Stations;
    return app::RemoteCatalogNodeKind::Root;
}

std::string nodeKindJson(app::RemoteCatalogNodeKind kind)
{
    switch (kind) {
    case app::RemoteCatalogNodeKind::Artists: return "artists";
    case app::RemoteCatalogNodeKind::Artist: return "artist";
    case app::RemoteCatalogNodeKind::Albums: return "albums";
    case app::RemoteCatalogNodeKind::Album: return "album";
    case app::RemoteCatalogNodeKind::Tracks: return "tracks";
    case app::RemoteCatalogNodeKind::Genres: return "genres";
    case app::RemoteCatalogNodeKind::Genre: return "genre";
    case app::RemoteCatalogNodeKind::Playlists: return "playlists";
    case app::RemoteCatalogNodeKind::Playlist: return "playlist";
    case app::RemoteCatalogNodeKind::Folders: return "folders";
    case app::RemoteCatalogNodeKind::Folder: return "folder";
    case app::RemoteCatalogNodeKind::Favorites: return "favorites";
    case app::RemoteCatalogNodeKind::RecentlyAdded: return "recently-added";
    case app::RemoteCatalogNodeKind::RecentlyPlayed: return "recently-played";
    case app::RemoteCatalogNodeKind::Stations: return "stations";
    case app::RemoteCatalogNodeKind::Root: return "root";
    }
    return "root";
}

std::vector<app::RemoteCatalogNode> parseRemoteNodes(std::string_view json)
{
    std::vector<app::RemoteCatalogNode> nodes{};
    std::size_t pos = json.find("\"nodes\":[");
    if (pos == std::string_view::npos) {
        return nodes;
    }

    pos = json.find('{', pos);
    while (pos != std::string_view::npos) {
        const auto end = json.find('}', pos);
        if (end == std::string_view::npos) {
            break;
        }
        const auto block = json.substr(pos, end - pos + 1);
        app::RemoteCatalogNode node{};
        if (const auto value = extractJsonString(block, "\"kind\":\"")) node.kind = parseNodeKind(*value);
        if (const auto value = extractJsonString(block, "\"id\":\"")) node.id = *value;
        if (const auto value = extractJsonString(block, "\"title\":\"")) node.title = repairMetadataText(*value);
        if (const auto value = extractJsonString(block, "\"subtitle\":\"")) node.subtitle = repairMetadataText(*value);
        if (const auto value = extractJsonString(block, "\"artist\":\"")) node.artist = repairMetadataText(*value);
        if (const auto value = extractJsonString(block, "\"album\":\"")) node.album = repairMetadataText(*value);
        if (const auto value = extractJsonString(block, "\"album_id\":\"")) node.album_id = *value;
        if (const auto value = extractJsonString(block, "\"source_id\":\"")) node.source_id = *value;
        if (const auto value = extractJsonString(block, "\"source_label\":\"")) node.source_label = repairMetadataText(*value);
        if (const auto value = extractJsonString(block, "\"artwork_key\":\"")) node.artwork_key = *value;
        if (const auto value = extractJsonString(block, "\"artwork_url\":\"")) node.artwork_url = *value;
        if (const auto value = extractJsonString(block, "\"lyrics_plain\":\"")) node.lyrics_plain = repairMetadataText(*value);
        if (const auto value = extractJsonString(block, "\"lyrics_synced\":\"")) node.lyrics_synced = repairMetadataText(*value);
        if (const auto value = extractJsonString(block, "\"lyrics_source\":\"")) node.lyrics_source = repairMetadataText(*value);
        if (const auto value = extractJsonString(block, "\"fingerprint\":\"")) node.fingerprint = *value;
        if (const auto duration = parseJsonInt(block, "\"duration_seconds\":")) node.duration_seconds = *duration;
        node.playable = parseJsonBool(block, "\"playable\":").value_or(false);
        node.browsable = parseJsonBool(block, "\"browsable\":").value_or(true);
        if (!node.id.empty() || !node.title.empty()) {
            nodes.push_back(node);
        }
        pos = json.find('{', end + 1);
    }
    return nodes;
}

std::optional<app::ResolvedRemoteStream> parseResolvedStream(std::string_view json)
{
    app::ResolvedRemoteStream stream{};
    if (const auto url = extractJsonString(json, "\"url\":\"")) {
        stream.url = *url;
    } else {
        return std::nullopt;
    }
    stream.seekable = parseJsonBool(json, "\"seekable\":").value_or(true);
    if (const auto source = extractJsonString(json, "\"source_label\":\"")) {
        stream.diagnostics.source_name = *source;
    }
    if (const auto redacted = extractJsonString(json, "\"resolved_url_redacted\":\"")) {
        stream.diagnostics.resolved_url_redacted = *redacted;
    }
    if (const auto codec = extractJsonString(json, "\"codec\":\"")) {
        stream.diagnostics.codec = *codec;
    }
    if (const auto status = extractJsonString(json, "\"connection_status\":\"")) {
        stream.diagnostics.connection_status = *status;
    }
    if (const auto bitrate = parseJsonInt(json, "\"bitrate_kbps\":")) {
        stream.diagnostics.bitrate_kbps = *bitrate;
    }
    if (const auto sample_rate = parseJsonInt(json, "\"sample_rate_hz\":")) {
        stream.diagnostics.sample_rate_hz = *sample_rate;
    }
    if (const auto channel_count = parseJsonInt(json, "\"channel_count\":")) {
        stream.diagnostics.channel_count = *channel_count;
    }
    stream.diagnostics.live = parseJsonBool(json, "\"live\":").value_or(false);
    stream.diagnostics.connected = parseJsonBool(json, "\"connected\":").value_or(!stream.url.empty());
    return stream;
}

app::ResolvedRemoteStream localResolvedStream(const app::RemoteServerProfile& profile, const app::RemoteTrack& track)
{
    const auto entry = ::lofibox::remote::StreamSourceClassifier::classify(track.id.empty() ? profile.base_url : track.id);
    app::ResolvedRemoteStream stream{};
    stream.url = entry.uri.empty() ? profile.base_url : entry.uri;
    stream.seekable = !isLiveKind(profile.kind);
    stream.quality_preference = app::StreamQualityPreference::Auto;
    stream.diagnostics.source_name = profile.name.empty() ? toKindString(profile.kind) : profile.name;
    stream.diagnostics.resolved_url_redacted = ::lofibox::security::SecretRedactor{}.redact(stream.url);
    stream.diagnostics.protocol = protocolFromDirectEntry(entry);
    stream.diagnostics.live = isLiveKind(profile.kind);
    stream.diagnostics.seekable = stream.seekable;
    stream.diagnostics.connected = !stream.url.empty();
    stream.diagnostics.connection_status = stream.diagnostics.connected ? "READY" : "MISSING URL";
    return stream;
}

} // namespace

RemoteMediaToolClient::RemoteMediaToolClient()
{
    python_path_ = resolvePythonPath();
    plugins::PluginRegistry registry;
    registry.discover(pluginSearchDirs());
    for (const auto& manifest : registry.manifests()) {
        if (manifest.kind == plugins::PluginKind::ExternalHelper
            && plugins::manifestHasCapability(manifest, "remote.source")
            && manifest.entry) {
            remote_plugins_.push_back(manifest);
        }
    }
}

bool RemoteMediaToolClient::available() const
{
    return !remote_plugins_.empty()
        || (python_path_.has_value() && fs::exists(remoteMediaToolScriptPath()));
}

const plugins::PluginManifest* RemoteMediaToolClient::pluginForProfile(const app::RemoteServerProfile& profile) const
{
    const auto kind = toKindString(profile.kind);
    const auto exact_id = "io.github.vicliu624.lofibox.remote." + kind;
    for (const auto& plugin : remote_plugins_) {
        if (plugin.id == exact_id || remotePluginShortName(plugin.id) == kind) {
            return &plugin;
        }
    }
    return nullptr;
}

plugins::PluginRuntime* RemoteMediaToolClient::runtimeForPlugin(const plugins::PluginManifest& manifest) const
{
    auto found = runtimes_.find(manifest.id);
    if (found != runtimes_.end()) {
        return found->second.get();
    }
    if (!manifest.entry) {
        return nullptr;
    }

    plugins::PluginRuntimeConfig config{};
    config.plugin_dir = manifest.source_dir;
    config.stderr_log_dir = runtime_paths::appStateDir() / "plugins" / pluginLogId(manifest.id);
    config.command = manifest.entry->command;
    config.args = manifest.entry->args;
    config.cwd = manifest.entry->cwd;
    config.env = manifest.entry->env;
    auto runtime = std::make_unique<plugins::PluginRuntime>(std::move(config));
    auto* ptr = runtime.get();
    runtimes_.emplace(manifest.id, std::move(runtime));
    return ptr;
}

std::optional<std::string> RemoteMediaToolClient::callPlugin(
    const app::RemoteServerProfile& profile,
    std::string_view method,
    std::string_view params_json) const
{
    const auto* plugin = pluginForProfile(profile);
    if (plugin == nullptr) {
        return std::nullopt;
    }
    auto* runtime = runtimeForPlugin(*plugin);
    if (runtime == nullptr) {
        return std::nullopt;
    }
    auto response = runtime->call(method, params_json);
    if (!response) {
        logRuntime(RuntimeLogLevel::Warn, "remote", "Plugin " + plugin->id + " failed: " + runtime->lastError());
    }
    return response;
}

app::RemoteSourceSession RemoteMediaToolClient::probe(const app::RemoteServerProfile& profile) const
{
    if (handledLocally(profile.kind)) {
        return app::RemoteSourceSession{!profile.base_url.empty(), profile.name, "", "", profile.base_url.empty() ? "MISSING_URL" : "OK"};
    }
    if (const auto json = callPlugin(profile, "probe", std::string("{\"profile\":") + remoteProfileJson(profile) + "}")) {
        auto session = parseRemoteSession(*json);
        logRuntime(session.available ? RuntimeLogLevel::Info : RuntimeLogLevel::Warn, "remote", "Plugin probe " + profile.name + " -> " + (session.available ? "available" : "unavailable"));
        return session;
    }
    if (!legacyToolAvailable(python_path_)) {
        logRuntime(RuntimeLogLevel::Warn, "remote", "Remote source provider unavailable");
        return {};
    }
    const std::string payload = std::string("{\"action\":\"probe\",\"profile\":") + remoteProfileJson(profile) + "}";
    if (const auto json = callRemoteMediaTool(*python_path_, payload)) {
        auto session = parseRemoteSession(*json);
        logRuntime(session.available ? RuntimeLogLevel::Info : RuntimeLogLevel::Warn, "remote", "Probe " + profile.name + " -> " + (session.available ? "available" : "unavailable"));
        return session;
    }
    logRuntime(RuntimeLogLevel::Warn, "remote", "Probe failed for " + profile.name);
    return {};
}

std::vector<app::RemoteTrack> RemoteMediaToolClient::searchTracks(
    const app::RemoteServerProfile& profile,
    const app::RemoteSourceSession& session,
    std::string_view query,
    int limit) const
{
    if (handledLocally(profile.kind)) {
        auto track = localRemoteTrack(profile);
        const auto query_text = std::string(query);
        if (query_text.empty() || track.title.find(query_text) != std::string::npos || track.id.find(query_text) != std::string::npos) {
            return {track};
        }
        return {};
    }
    {
        std::ostringstream params;
        params << "{\"profile\":" << remoteProfileJson(profile)
               << ",\"session\":" << remoteSessionJson(session)
               << ",\"query\":\"" << jsonEscape(std::string(query))
               << "\",\"limit\":" << limit << "}";
        if (const auto json = callPlugin(profile, "search", params.str())) {
            auto tracks = parseRemoteTracks(*json);
            logRuntime(RuntimeLogLevel::Info, "remote", "Plugin search " + profile.name + " returned " + std::to_string(tracks.size()) + " tracks");
            return tracks;
        }
    }
    if (!legacyToolAvailable(python_path_)) {
        logRuntime(RuntimeLogLevel::Warn, "remote", "Remote catalog provider unavailable");
        return {};
    }
    std::ostringstream payload;
    payload << "{\"action\":\"search\",\"profile\":" << remoteProfileJson(profile)
            << ",\"session\":" << remoteSessionJson(session)
            << ",\"query\":\"" << jsonEscape(std::string(query))
            << "\",\"limit\":" << limit << "}";
    if (const auto json = callRemoteMediaTool(*python_path_, payload.str())) {
        auto tracks = parseRemoteTracks(*json);
        logRuntime(RuntimeLogLevel::Info, "remote", "Search " + profile.name + " returned " + std::to_string(tracks.size()) + " tracks");
        return tracks;
    }
    logRuntime(RuntimeLogLevel::Warn, "remote", "Search failed for " + profile.name);
    return {};
}

std::vector<app::RemoteTrack> RemoteMediaToolClient::recentTracks(
    const app::RemoteServerProfile& profile,
    const app::RemoteSourceSession& session,
    int limit) const
{
    if (handledLocally(profile.kind)) {
        return profile.base_url.empty() ? std::vector<app::RemoteTrack>{} : std::vector<app::RemoteTrack>{localRemoteTrack(profile)};
    }
    {
        std::ostringstream params;
        params << "{\"profile\":" << remoteProfileJson(profile)
               << ",\"session\":" << remoteSessionJson(session)
               << ",\"limit\":" << limit << "}";
        if (const auto json = callPlugin(profile, "recent", params.str())) {
            auto tracks = parseRemoteTracks(*json);
            logRuntime(RuntimeLogLevel::Info, "remote", "Plugin recent " + profile.name + " returned " + std::to_string(tracks.size()) + " tracks");
            return tracks;
        }
    }
    if (!legacyToolAvailable(python_path_)) {
        logRuntime(RuntimeLogLevel::Warn, "remote", "Remote catalog provider unavailable");
        return {};
    }
    std::ostringstream payload;
    payload << "{\"action\":\"recent\",\"profile\":" << remoteProfileJson(profile)
            << ",\"session\":" << remoteSessionJson(session)
            << ",\"limit\":" << limit << "}";
    if (const auto json = callRemoteMediaTool(*python_path_, payload.str())) {
        auto tracks = parseRemoteTracks(*json);
        logRuntime(RuntimeLogLevel::Info, "remote", "Recent " + profile.name + " returned " + std::to_string(tracks.size()) + " tracks");
        return tracks;
    }
    logRuntime(RuntimeLogLevel::Warn, "remote", "Recent query failed for " + profile.name);
    return {};
}

std::vector<app::RemoteTrack> RemoteMediaToolClient::libraryTracks(
    const app::RemoteServerProfile& profile,
    const app::RemoteSourceSession& session,
    int limit) const
{
    if (handledLocally(profile.kind)) {
        return profile.base_url.empty() ? std::vector<app::RemoteTrack>{} : std::vector<app::RemoteTrack>{localRemoteTrack(profile)};
    }
    {
        std::ostringstream params;
        params << "{\"profile\":" << remoteProfileJson(profile)
               << ",\"session\":" << remoteSessionJson(session)
               << ",\"limit\":" << limit << "}";
        if (const auto json = callPlugin(profile, "library_tracks", params.str())) {
            auto tracks = parseRemoteTracks(*json);
            logRuntime(RuntimeLogLevel::Info, "remote", "Plugin library " + profile.name + " returned " + std::to_string(tracks.size()) + " tracks");
            return tracks;
        }
    }
    if (!legacyToolAvailable(python_path_)) {
        logRuntime(RuntimeLogLevel::Warn, "remote", "Remote catalog provider unavailable");
        return {};
    }
    std::ostringstream payload;
    payload << "{\"action\":\"library_tracks\",\"profile\":" << remoteProfileJson(profile)
            << ",\"session\":" << remoteSessionJson(session)
            << ",\"limit\":" << limit << "}";
    if (const auto json = callRemoteMediaTool(*python_path_, payload.str())) {
        auto tracks = parseRemoteTracks(*json);
        logRuntime(RuntimeLogLevel::Info, "remote", "Library " + profile.name + " returned " + std::to_string(tracks.size()) + " tracks");
        return tracks;
    }
    logRuntime(RuntimeLogLevel::Warn, "remote", "Library query failed for " + profile.name);
    return {};
}

std::vector<app::RemoteCatalogNode> RemoteMediaToolClient::browse(
    const app::RemoteServerProfile& profile,
    const app::RemoteSourceSession& session,
    const app::RemoteCatalogNode& parent,
    int limit) const
{
    if (handledLocally(profile.kind)) {
        return profile.base_url.empty() ? std::vector<app::RemoteCatalogNode>{} : std::vector<app::RemoteCatalogNode>{localRemoteNode(profile)};
    }
    {
        std::ostringstream params;
        params << "{\"profile\":" << remoteProfileJson(profile)
               << ",\"session\":" << remoteSessionJson(session)
               << ",\"parent\":{\"kind\":\"" << nodeKindJson(parent.kind)
               << "\",\"id\":\"" << jsonEscape(parent.id)
               << "\",\"title\":\"" << jsonEscape(parent.title)
               << "\"},\"limit\":" << limit << "}";
        if (const auto json = callPlugin(profile, "browse", params.str())) {
            auto nodes = parseRemoteNodes(*json);
            logRuntime(RuntimeLogLevel::Info, "remote", "Plugin browse " + profile.name + " returned " + std::to_string(nodes.size()) + " nodes");
            return nodes;
        }
    }
    if (!legacyToolAvailable(python_path_)) {
        logRuntime(RuntimeLogLevel::Warn, "remote", "Remote catalog provider unavailable");
        return {};
    }
    std::ostringstream payload;
    payload << "{\"action\":\"browse\",\"profile\":" << remoteProfileJson(profile)
            << ",\"session\":" << remoteSessionJson(session)
            << ",\"parent\":{\"kind\":\"" << nodeKindJson(parent.kind)
            << "\",\"id\":\"" << jsonEscape(parent.id)
            << "\",\"title\":\"" << jsonEscape(parent.title)
            << "\"},\"limit\":" << limit << "}";
    if (const auto json = callRemoteMediaTool(*python_path_, payload.str())) {
        auto nodes = parseRemoteNodes(*json);
        logRuntime(RuntimeLogLevel::Info, "remote", "Browse " + profile.name + " returned " + std::to_string(nodes.size()) + " nodes");
        return nodes;
    }
    logRuntime(RuntimeLogLevel::Warn, "remote", "Browse failed for " + profile.name);
    return {};
}

std::optional<app::ResolvedRemoteStream> RemoteMediaToolClient::resolveTrack(
    const app::RemoteServerProfile& profile,
    const app::RemoteSourceSession& session,
    const app::RemoteTrack& track) const
{
    if (handledLocally(profile.kind)) {
        return localResolvedStream(profile, track);
    }
    {
        std::ostringstream params;
        params << "{\"profile\":" << remoteProfileJson(profile)
               << ",\"session\":" << remoteSessionJson(session)
               << ",\"track\":{\"id\":\"" << jsonEscape(track.id)
               << "\",\"title\":\"" << jsonEscape(track.title)
               << "\",\"artist\":\"" << jsonEscape(track.artist)
               << "\",\"album\":\"" << jsonEscape(track.album)
               << "\",\"album_id\":\"" << jsonEscape(track.album_id)
               << "\",\"duration_seconds\":" << track.duration_seconds
               << ",\"source_id\":\"" << jsonEscape(track.source_id)
               << "\",\"source_label\":\"" << jsonEscape(track.source_label)
               << "\",\"artwork_key\":\"" << jsonEscape(track.artwork_key)
               << "\",\"artwork_url\":\"" << jsonEscape(track.artwork_url)
               << "\",\"lyrics_plain\":\"" << jsonEscape(track.lyrics_plain)
               << "\",\"lyrics_synced\":\"" << jsonEscape(track.lyrics_synced)
               << "\",\"lyrics_source\":\"" << jsonEscape(track.lyrics_source)
               << "\",\"fingerprint\":\"" << jsonEscape(track.fingerprint)
               << "\"}}";
        if (const auto json = callPlugin(profile, "resolve", params.str())) {
            auto resolved = parseResolvedStream(*json);
            if (resolved) {
                const auto entry = ::lofibox::remote::StreamSourceClassifier::classify(resolved->url);
                resolved->quality_preference = app::StreamQualityPreference::Auto;
                if (resolved->diagnostics.source_name.empty()) {
                    resolved->diagnostics.source_name = profile.name.empty() ? toKindString(profile.kind) : profile.name;
                }
                if (resolved->diagnostics.resolved_url_redacted.empty()) {
                    resolved->diagnostics.resolved_url_redacted = ::lofibox::security::SecretRedactor{}.redact(resolved->url);
                }
                resolved->diagnostics.protocol = protocolFromDirectEntry(entry);
                resolved->diagnostics.seekable = resolved->seekable;
                resolved->diagnostics.connected = true;
                if (resolved->diagnostics.connection_status.empty()) {
                    resolved->diagnostics.connection_status = "READY";
                }
            }
            logRuntime(resolved ? RuntimeLogLevel::Info : RuntimeLogLevel::Warn, "remote", std::string("Plugin resolve ") + profile.name + " track " + track.id + (resolved ? " succeeded" : " failed"));
            return resolved;
        }
    }
    if (!legacyToolAvailable(python_path_)) {
        logRuntime(RuntimeLogLevel::Warn, "remote", "Remote stream resolver unavailable");
        return std::nullopt;
    }
    std::ostringstream payload;
    payload << "{\"action\":\"resolve\",\"profile\":" << remoteProfileJson(profile)
            << ",\"session\":" << remoteSessionJson(session)
            << ",\"track\":{\"id\":\"" << jsonEscape(track.id)
            << "\",\"title\":\"" << jsonEscape(track.title)
            << "\",\"artist\":\"" << jsonEscape(track.artist)
            << "\",\"album\":\"" << jsonEscape(track.album)
            << "\",\"album_id\":\"" << jsonEscape(track.album_id)
            << "\",\"duration_seconds\":" << track.duration_seconds
            << ",\"source_id\":\"" << jsonEscape(track.source_id)
            << "\",\"source_label\":\"" << jsonEscape(track.source_label)
            << "\",\"artwork_key\":\"" << jsonEscape(track.artwork_key)
            << "\",\"artwork_url\":\"" << jsonEscape(track.artwork_url)
            << "\",\"lyrics_plain\":\"" << jsonEscape(track.lyrics_plain)
            << "\",\"lyrics_synced\":\"" << jsonEscape(track.lyrics_synced)
            << "\",\"lyrics_source\":\"" << jsonEscape(track.lyrics_source)
            << "\",\"fingerprint\":\"" << jsonEscape(track.fingerprint)
            << "\"}}";
    if (const auto json = callRemoteMediaTool(*python_path_, payload.str())) {
        auto resolved = parseResolvedStream(*json);
        if (resolved) {
            const auto entry = ::lofibox::remote::StreamSourceClassifier::classify(resolved->url);
            resolved->quality_preference = app::StreamQualityPreference::Auto;
            if (resolved->diagnostics.source_name.empty()) {
                resolved->diagnostics.source_name = profile.name.empty() ? toKindString(profile.kind) : profile.name;
            }
            if (resolved->diagnostics.resolved_url_redacted.empty()) {
                resolved->diagnostics.resolved_url_redacted = ::lofibox::security::SecretRedactor{}.redact(resolved->url);
            }
            resolved->diagnostics.protocol = protocolFromDirectEntry(entry);
            resolved->diagnostics.seekable = resolved->seekable;
            resolved->diagnostics.connected = true;
            if (resolved->diagnostics.connection_status.empty()) {
                resolved->diagnostics.connection_status = "READY";
            }
        }
        logRuntime(resolved ? RuntimeLogLevel::Info : RuntimeLogLevel::Warn, "remote", std::string("Resolve ") + profile.name + " track " + track.id + (resolved ? " succeeded" : " failed"));
        return resolved;
    }
    logRuntime(RuntimeLogLevel::Warn, "remote", "Resolve failed for " + profile.name + " track " + track.id);
    return std::nullopt;
}

} // namespace lofibox::platform::host::runtime_detail
