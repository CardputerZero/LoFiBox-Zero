// SPDX-License-Identifier: GPL-3.0-or-later

#include "plugins/remote_plugin_adapter.h"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <sstream>
#include <string>

#include "app/remote_profile_store.h"
#include "platform/host/runtime_host_internal.h"
#include "remote/common/stream_source_model.h"
#include "security/credential_policy.h"

namespace lofibox::plugins {
namespace {

using namespace platform::host::runtime_detail;

std::string quoteJson(const std::string& s)
{
    return "\"" + jsonEscape(s) + "\"";
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

} // namespace

RemotePluginAdapter::RemotePluginAdapter(PluginManifest manifest,
                                           PluginRuntime runtime)
    : manifest_(std::move(manifest))
    , runtime_(std::move(runtime))
{
}

bool RemotePluginAdapter::available() const
{
    return manifest_.entry.has_value();
}

std::string RemotePluginAdapter::displayName() const
{
    return manifest_.name;
}

std::string RemotePluginAdapter::profileJson(const app::RemoteServerProfile& profile) const
{
    std::ostringstream out;
    out << "{"
        << "\"kind\":\"" << jsonEscape(app::remoteServerKindToString(profile.kind)) << "\","
        << quoteJson("id") << ":" << quoteJson(profile.id) << ","
        << quoteJson("name") << ":" << quoteJson(profile.name) << ","
        << quoteJson("base_url") << ":" << quoteJson(profile.base_url) << ","
        << quoteJson("username") << ":" << quoteJson(profile.username) << ","
        << quoteJson("password") << ":" << quoteJson(profile.password) << ","
        << quoteJson("api_token") << ":" << quoteJson(profile.api_token)
        << "}";
    return out.str();
}

std::string RemotePluginAdapter::sessionJson(const app::RemoteSourceSession& session) const
{
    std::ostringstream out;
    out << "{"
        << "\"available\":" << (session.available ? "true" : "false") << ","
        << quoteJson("server_name") << ":" << quoteJson(session.server_name) << ","
        << quoteJson("user_id") << ":" << quoteJson(session.user_id) << ","
        << quoteJson("access_token") << ":" << quoteJson(session.access_token) << ","
        << quoteJson("message") << ":" << quoteJson(session.message)
        << "}";
    return out.str();
}

std::string RemotePluginAdapter::trackJson(const app::RemoteTrack& track) const
{
    std::ostringstream out;
    out << "{"
        << quoteJson("id") << ":" << quoteJson(track.id) << ","
        << quoteJson("title") << ":" << quoteJson(track.title) << ","
        << quoteJson("artist") << ":" << quoteJson(track.artist) << ","
        << quoteJson("album") << ":" << quoteJson(track.album) << ","
        << quoteJson("album_id") << ":" << quoteJson(track.album_id) << ","
        << "\"duration_seconds\":" << track.duration_seconds << ","
        << quoteJson("source_id") << ":" << quoteJson(track.source_id) << ","
        << quoteJson("source_label") << ":" << quoteJson(track.source_label)
        << "}";
    return out.str();
}

std::string RemotePluginAdapter::parentNodeJson(const app::RemoteCatalogNode& parent) const
{
    std::ostringstream out;
    out << "{"
        << quoteJson("kind") << ":" << quoteJson(std::string(
            parent.kind == app::RemoteCatalogNodeKind::Root ? "root" :
            parent.kind == app::RemoteCatalogNodeKind::Artists ? "artists" :
            parent.kind == app::RemoteCatalogNodeKind::Artist ? "artist" :
            parent.kind == app::RemoteCatalogNodeKind::Albums ? "albums" :
            parent.kind == app::RemoteCatalogNodeKind::Album ? "album" :
            parent.kind == app::RemoteCatalogNodeKind::Tracks ? "tracks" :
            parent.kind == app::RemoteCatalogNodeKind::Genres ? "genres" :
            parent.kind == app::RemoteCatalogNodeKind::Genre ? "genre" :
            parent.kind == app::RemoteCatalogNodeKind::Playlists ? "playlists" :
            parent.kind == app::RemoteCatalogNodeKind::Playlist ? "playlist" :
            parent.kind == app::RemoteCatalogNodeKind::Folders ? "folders" :
            parent.kind == app::RemoteCatalogNodeKind::Folder ? "folder" :
            parent.kind == app::RemoteCatalogNodeKind::Favorites ? "favorites" :
            parent.kind == app::RemoteCatalogNodeKind::RecentlyAdded ? "recently-added" :
            parent.kind == app::RemoteCatalogNodeKind::RecentlyPlayed ? "recently-played" :
            parent.kind == app::RemoteCatalogNodeKind::Stations ? "stations" : "root")) << ","
        << quoteJson("id") << ":" << quoteJson(parent.id) << ","
        << quoteJson("title") << ":" << quoteJson(parent.title)
        << "}";
    return out.str();
}

std::vector<app::RemoteTrack> RemotePluginAdapter::parseTracksResponse(std::string_view json) const
{
    std::vector<app::RemoteTrack> tracks{};
    std::size_t pos = json.find("\"tracks\":[");
    if (pos == std::string_view::npos) return tracks;

    pos = json.find('{', pos);
    while (pos != std::string_view::npos) {
        const auto end = json.find('}', pos);
        if (end == std::string_view::npos) break;
        const auto block = json.substr(pos, end - pos + 1);
        app::RemoteTrack track{};
        if (const auto v = extractJsonString(block, "\"id\":\"")) track.id = *v;
        if (const auto v = extractJsonString(block, "\"title\":\"")) track.title = repairMetadataText(*v);
        if (const auto v = extractJsonString(block, "\"artist\":\"")) track.artist = repairMetadataText(*v);
        if (const auto v = extractJsonString(block, "\"album\":\"")) track.album = repairMetadataText(*v);
        if (const auto v = extractJsonString(block, "\"album_id\":\"")) track.album_id = *v;
        if (const auto v = extractJsonString(block, "\"source_id\":\"")) track.source_id = *v;
        if (const auto v = extractJsonString(block, "\"source_label\":\"")) track.source_label = repairMetadataText(*v);
        if (const auto v = extractJsonString(block, "\"artwork_key\":\"")) track.artwork_key = *v;
        if (const auto v = extractJsonString(block, "\"artwork_url\":\"")) track.artwork_url = *v;
        if (const auto v = extractJsonString(block, "\"lyrics_plain\":\"")) track.lyrics_plain = repairMetadataText(*v);
        if (const auto v = extractJsonString(block, "\"lyrics_synced\":\"")) track.lyrics_synced = repairMetadataText(*v);
        if (const auto v = extractJsonString(block, "\"lyrics_source\":\"")) track.lyrics_source = repairMetadataText(*v);
        if (const auto v = extractJsonString(block, "\"fingerprint\":\"")) track.fingerprint = *v;
        if (const auto dur = parseJsonInt(block, "\"duration_seconds\":")) track.duration_seconds = *dur;
        if (!track.id.empty()) tracks.push_back(track);
        pos = json.find('{', end + 1);
    }
    return tracks;
}

std::vector<app::RemoteCatalogNode> RemotePluginAdapter::parseNodesResponse(std::string_view json) const
{
    std::vector<app::RemoteCatalogNode> nodes{};
    std::size_t pos = json.find("\"nodes\":[");
    if (pos == std::string_view::npos) return nodes;

    pos = json.find('{', pos);
    while (pos != std::string_view::npos) {
        const auto end = json.find('}', pos);
        if (end == std::string_view::npos) break;
        const auto block = json.substr(pos, end - pos + 1);
        app::RemoteCatalogNode node{};
        if (const auto v = extractJsonString(block, "\"kind\":\"")) node.kind = parseNodeKind(*v);
        if (const auto v = extractJsonString(block, "\"id\":\"")) node.id = *v;
        if (const auto v = extractJsonString(block, "\"title\":\"")) node.title = repairMetadataText(*v);
        if (const auto v = extractJsonString(block, "\"subtitle\":\"")) node.subtitle = repairMetadataText(*v);
        if (const auto v = extractJsonString(block, "\"artist\":\"")) node.artist = repairMetadataText(*v);
        if (const auto v = extractJsonString(block, "\"album\":\"")) node.album = repairMetadataText(*v);
        if (const auto v = extractJsonString(block, "\"album_id\":\"")) node.album_id = *v;
        if (const auto v = extractJsonString(block, "\"source_id\":\"")) node.source_id = *v;
        if (const auto v = extractJsonString(block, "\"source_label\":\"")) node.source_label = repairMetadataText(*v);
        if (const auto v = extractJsonString(block, "\"artwork_key\":\"")) node.artwork_key = *v;
        if (const auto v = extractJsonString(block, "\"artwork_url\":\"")) node.artwork_url = *v;
        if (const auto v = extractJsonString(block, "\"lyrics_plain\":\"")) node.lyrics_plain = repairMetadataText(*v);
        if (const auto v = extractJsonString(block, "\"lyrics_synced\":\"")) node.lyrics_synced = repairMetadataText(*v);
        if (const auto v = extractJsonString(block, "\"lyrics_source\":\"")) node.lyrics_source = repairMetadataText(*v);
        if (const auto v = extractJsonString(block, "\"fingerprint\":\"")) node.fingerprint = *v;
        if (const auto dur = parseJsonInt(block, "\"duration_seconds\":")) node.duration_seconds = *dur;
        node.playable = parseJsonBool(block, "\"playable\":").value_or(false);
        node.browsable = parseJsonBool(block, "\"browsable\":").value_or(true);
        if (!node.id.empty() || !node.title.empty()) nodes.push_back(node);
        pos = json.find('{', end + 1);
    }
    return nodes;
}

std::optional<app::ResolvedRemoteStream> RemotePluginAdapter::parseStreamResponse(std::string_view json) const
{
    app::ResolvedRemoteStream stream{};
    if (const auto url = extractJsonString(json, "\"url\":\"")) {
        stream.url = *url;
    } else {
        return std::nullopt;
    }
    stream.seekable = parseJsonBool(json, "\"seekable\":").value_or(true);
    if (const auto v = extractJsonString(json, "\"source_label\":\"")) stream.diagnostics.source_name = *v;
    if (const auto v = extractJsonString(json, "\"codec\":\"")) stream.diagnostics.codec = *v;
    if (const auto v = extractJsonString(json, "\"connection_status\":\"")) stream.diagnostics.connection_status = *v;
    if (const auto v = parseJsonInt(json, "\"bitrate_kbps\":")) stream.diagnostics.bitrate_kbps = *v;
    if (const auto v = parseJsonInt(json, "\"sample_rate_hz\":")) stream.diagnostics.sample_rate_hz = *v;
    if (const auto v = parseJsonInt(json, "\"channel_count\":")) stream.diagnostics.channel_count = *v;
    stream.diagnostics.live = parseJsonBool(json, "\"live\":").value_or(false);
    stream.diagnostics.connected = parseJsonBool(json, "\"connected\":").value_or(true);

    const auto entry = ::lofibox::remote::StreamSourceClassifier::classify(stream.url);
    stream.diagnostics.resolved_url_redacted = ::lofibox::security::SecretRedactor{}.redact(stream.url);
    stream.diagnostics.seekable = stream.seekable;
    if (stream.diagnostics.connection_status.empty()) {
        stream.diagnostics.connection_status = "READY";
    }
    return stream;
}

app::RemoteSourceSession RemotePluginAdapter::parseSessionResponse(std::string_view json) const
{
    app::RemoteSourceSession session{};
    if (const auto v = extractJsonString(json, "\"server_name\":\"")) session.server_name = repairMetadataText(*v);
    if (const auto v = extractJsonString(json, "\"user_id\":\"")) session.user_id = *v;
    if (const auto v = extractJsonString(json, "\"access_token\":\"")) session.access_token = *v;
    if (const auto v = extractJsonString(json, "\"message\":\"")) session.message = repairMetadataText(*v);
    session.available = parseJsonBool(json, "\"available\":").value_or(false);
    return session;
}

// RemoteSourceProvider

app::RemoteSourceSession RemotePluginAdapter::probe(const app::RemoteServerProfile& profile) const
{
    auto resp = runtime_.call("probe", std::string{"{\"profile\":"} + profileJson(profile) + "}");
    if (!resp) return {};
    return parseSessionResponse(*resp);
}

// RemoteCatalogProvider

std::vector<app::RemoteTrack> RemotePluginAdapter::searchTracks(
    const app::RemoteServerProfile& profile,
    const app::RemoteSourceSession& session,
    std::string_view query,
    int limit) const
{
    std::ostringstream params;
    params << "{" << quoteJson("profile") << ":" << profileJson(profile)
           << "," << quoteJson("session") << ":" << sessionJson(session)
           << "," << quoteJson("query") << ":" << quoteJson(std::string(query))
           << ",\"limit\":" << limit << "}";
    auto resp = runtime_.call("search", params.str());
    if (!resp) return {};
    return parseTracksResponse(*resp);
}

std::vector<app::RemoteTrack> RemotePluginAdapter::recentTracks(
    const app::RemoteServerProfile& profile,
    const app::RemoteSourceSession& session,
    int limit) const
{
    std::ostringstream params;
    params << "{" << quoteJson("profile") << ":" << profileJson(profile)
           << "," << quoteJson("session") << ":" << sessionJson(session)
           << ",\"limit\":" << limit << "}";
    auto resp = runtime_.call("recent", params.str());
    if (!resp) return {};
    return parseTracksResponse(*resp);
}

std::vector<app::RemoteTrack> RemotePluginAdapter::libraryTracks(
    const app::RemoteServerProfile& profile,
    const app::RemoteSourceSession& session,
    int limit) const
{
    std::ostringstream params;
    params << "{" << quoteJson("profile") << ":" << profileJson(profile)
           << "," << quoteJson("session") << ":" << sessionJson(session)
           << ",\"limit\":" << limit << "}";
    auto resp = runtime_.call("library_tracks", params.str());
    if (!resp) return {};
    return parseTracksResponse(*resp);
}

std::vector<app::RemoteCatalogNode> RemotePluginAdapter::browse(
    const app::RemoteServerProfile& profile,
    const app::RemoteSourceSession& session,
    const app::RemoteCatalogNode& parent,
    int limit) const
{
    std::ostringstream params;
    params << "{" << quoteJson("profile") << ":" << profileJson(profile)
           << "," << quoteJson("session") << ":" << sessionJson(session)
           << "," << quoteJson("parent") << ":" << parentNodeJson(parent)
           << ",\"limit\":" << limit << "}";
    auto resp = runtime_.call("browse", params.str());
    if (!resp) return {};
    return parseNodesResponse(*resp);
}

// RemoteStreamResolver

std::optional<app::ResolvedRemoteStream> RemotePluginAdapter::resolveTrack(
    const app::RemoteServerProfile& profile,
    const app::RemoteSourceSession& session,
    const app::RemoteTrack& track) const
{
    std::ostringstream params;
    params << "{" << quoteJson("profile") << ":" << profileJson(profile)
           << "," << quoteJson("session") << ":" << sessionJson(session)
           << "," << quoteJson("track") << ":" << trackJson(track) << "}";
    auto resp = runtime_.call("resolve", params.str());
    if (!resp) return std::nullopt;
    return parseStreamResponse(*resp);
}

// Plugin-specific

std::string RemotePluginAdapter::profileSchemaJson() const
{
    auto resp = runtime_.call("profile_schema", "{}");
    if (!resp) return "{}";
    auto result_pos = resp->find("\"result\":");
    if (result_pos == std::string::npos) return "{}";
    auto brace = resp->find('{', result_pos);
    if (brace == std::string::npos) return "{}";
    int depth = 0;
    auto end = brace;
    while (end < resp->size()) {
        if ((*resp)[end] == '{') ++depth;
        else if ((*resp)[end] == '}') { --depth; if (depth == 0) break; }
        ++end;
    }
    return std::string(resp->substr(brace, end - brace + 1));
}

app::RemoteServerKind RemotePluginAdapter::serverKind() const
{
    const auto dot = manifest_.id.rfind('.');
    if (dot != std::string::npos && dot + 1 < manifest_.id.size()) {
        return app::remoteServerKindFromString(manifest_.id.substr(dot + 1));
    }
    return app::remoteServerKindFromString(manifest_.id);
}

// RemotePluginRegistry

void RemotePluginRegistry::registerAdapter(std::unique_ptr<RemotePluginAdapter> adapter)
{
    if (!adapter) return;
    for (const auto& existing : adapters_) {
        if (existing->manifest().id == adapter->manifest().id) {
            return;
        }
    }
    adapters_.push_back(std::move(adapter));
}

RemotePluginAdapter* RemotePluginRegistry::findForKind(app::RemoteServerKind kind) const
{
    for (const auto& a : adapters_) {
        if (a->serverKind() == kind) return a.get();
    }
    return nullptr;
}

RemotePluginAdapter* RemotePluginRegistry::findForKindString(std::string_view kind_str) const
{
    for (const auto& a : adapters_) {
        const auto& id = a->manifest().id;
        const auto dot = id.rfind('.');
        const auto short_name = dot == std::string::npos ? std::string_view{id} : std::string_view{id}.substr(dot + 1);
        if (id == kind_str || short_name == kind_str) return a.get();
    }
    return nullptr;
}

const std::vector<std::unique_ptr<RemotePluginAdapter>>& RemotePluginRegistry::adapters() const noexcept
{
    return adapters_;
}

void RemotePluginRegistry::discoverFromDir(const std::filesystem::path& dir)
{
    if (!std::filesystem::exists(dir)) return;
    PluginRegistry registry;
    registry.discover({dir});
    for (const auto& manifest : registry.manifests()) {
        if (manifest.kind != PluginKind::ExternalHelper
            || !manifestHasCapability(manifest, "remote.source")
            || !manifest.entry) {
            continue;
        }

        PluginRuntimeConfig config{};
        config.plugin_dir = manifest.source_dir;
        config.stderr_log_dir = manifest.source_dir / ".lofibox-logs";
        config.command = manifest.entry->command;
        config.args = manifest.entry->args;
        config.cwd = manifest.entry->cwd;
        config.env = manifest.entry->env;

        registerAdapter(std::make_unique<RemotePluginAdapter>(
            manifest,
            PluginRuntime{std::move(config)}));
    }
}

} // namespace lofibox::plugins
