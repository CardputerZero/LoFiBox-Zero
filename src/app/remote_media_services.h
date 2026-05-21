// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "security/credential_policy.h"

namespace lofibox::app {

enum class RemoteServerKind {
    LocalRoot,
    Jellyfin,
    OpenSubsonic,
    Navidrome,
    Emby,
    DirectUrl,
    InternetRadio,
    PlaylistManifest,
    Hls,
    Dash,
    Smb,
    Nfs,
    WebDav,
    Ftp,
    Sftp,
    DlnaUpnp,
};

struct RemoteServerProfile {
    RemoteServerKind kind{RemoteServerKind::Jellyfin};
    std::string id{};
    std::string name{};
    std::string base_url{};
    std::string local_root{};
    std::string username{};
    std::string password{};
    std::string api_token{};
    ::lofibox::security::CredentialRef credential_ref{};
    ::lofibox::security::TlsPolicy tls_policy{};
    bool default_eligible{true};
    bool enabled{true};
};

struct RemoteSourceSession {
    bool available{false};
    std::string server_name{};
    std::string user_id{};
    std::string access_token{};
    std::string message{};
};

struct RemoteTrack {
    std::string id{};
    std::string title{};
    std::string artist{};
    std::string album{};
    std::string album_id{};
    int duration_seconds{0};
    std::string source_id{};
    std::string source_label{};
    std::string artwork_key{};
    std::string artwork_url{};
    std::string lyrics_plain{};
    std::string lyrics_synced{};
    std::string lyrics_source{};
    std::string fingerprint{};
};

enum class RemoteCatalogNodeKind {
    Root,
    Artists,
    Artist,
    Albums,
    Album,
    Tracks,
    Genres,
    Genre,
    Playlists,
    Playlist,
    Folders,
    Folder,
    Favorites,
    RecentlyAdded,
    RecentlyPlayed,
    Stations,
};

struct RemoteCatalogNode {
    RemoteCatalogNodeKind kind{RemoteCatalogNodeKind::Root};
    std::string id{};
    std::string title{};
    std::string subtitle{};
    bool playable{false};
    bool browsable{true};
    std::string artist{};
    std::string album{};
    int duration_seconds{0};
    std::string album_id{};
    std::string source_id{};
    std::string source_label{};
    std::string artwork_key{};
    std::string artwork_url{};
    std::string lyrics_plain{};
    std::string lyrics_synced{};
    std::string lyrics_source{};
    std::string fingerprint{};
};

enum class StreamProtocol {
    File,
    Http,
    Https,
    Hls,
    Dash,
    Icecast,
    Shoutcast,
    Smb,
    Nfs,
    WebDav,
    Ftp,
    Sftp,
    DlnaUpnp,
};

enum class StreamQualityPreference {
    Original,
    Auto,
    LowBandwidth,
    Manual,
};

struct RemoteStreamDiagnostics {
    std::string source_name{};
    std::string resolved_url_redacted{};
    StreamProtocol protocol{StreamProtocol::Http};
    int bitrate_kbps{0};
    std::string codec{};
    int sample_rate_hz{0};
    int channel_count{0};
    bool live{false};
    bool seekable{true};
    bool connected{false};
    std::string connection_status{};
};

struct ResolvedRemoteStream {
    std::string url{};
    std::vector<std::pair<std::string, std::string>> headers{};
    bool seekable{true};
    StreamQualityPreference quality_preference{StreamQualityPreference::Auto};
    RemoteStreamDiagnostics diagnostics{};
};

class RemoteSourceProvider {
public:
    virtual ~RemoteSourceProvider() = default;

    [[nodiscard]] virtual bool available() const = 0;
    [[nodiscard]] virtual std::string displayName() const = 0;
    [[nodiscard]] virtual RemoteSourceSession probe(const RemoteServerProfile& profile) const = 0;
};

class RemoteCatalogProvider {
public:
    virtual ~RemoteCatalogProvider() = default;

    [[nodiscard]] virtual bool available() const = 0;
    [[nodiscard]] virtual std::string displayName() const = 0;
    [[nodiscard]] virtual std::vector<RemoteTrack> searchTracks(
        const RemoteServerProfile& profile,
        const RemoteSourceSession& session,
        std::string_view query,
        int limit) const = 0;
    [[nodiscard]] virtual std::vector<RemoteTrack> recentTracks(
        const RemoteServerProfile& profile,
        const RemoteSourceSession& session,
        int limit) const = 0;
    [[nodiscard]] virtual std::vector<RemoteTrack> libraryTracks(
        const RemoteServerProfile& profile,
        const RemoteSourceSession& session,
        int limit) const
    {
        (void)profile;
        (void)session;
        (void)limit;
        return {};
    }
    [[nodiscard]] virtual std::vector<RemoteCatalogNode> browse(
        const RemoteServerProfile& profile,
        const RemoteSourceSession& session,
        const RemoteCatalogNode& parent,
        int limit) const
    {
        (void)profile;
        (void)session;
        (void)parent;
        (void)limit;
        return {};
    }
};

class RemoteStreamResolver {
public:
    virtual ~RemoteStreamResolver() = default;

    [[nodiscard]] virtual bool available() const = 0;
    [[nodiscard]] virtual std::string displayName() const = 0;
    [[nodiscard]] virtual std::optional<ResolvedRemoteStream> resolveTrack(
        const RemoteServerProfile& profile,
        const RemoteSourceSession& session,
        const RemoteTrack& track) const = 0;
};

} // namespace lofibox::app
