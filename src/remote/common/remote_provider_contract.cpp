// SPDX-License-Identifier: GPL-3.0-or-later

#include "remote/common/remote_provider_contract.h"

#include <algorithm>

namespace lofibox::remote {

std::string_view remoteServerKindId(app::RemoteServerKind kind) noexcept
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
    return "unsupported";
}

std::string_view remoteProviderFamily(app::RemoteServerKind kind) noexcept
{
    switch (kind) {
    case app::RemoteServerKind::LocalRoot: return "local-root";
    case app::RemoteServerKind::Jellyfin: return "jellyfin";
    case app::RemoteServerKind::OpenSubsonic:
    case app::RemoteServerKind::Navidrome:
        return "opensubsonic";
    case app::RemoteServerKind::Emby: return "emby";
    case app::RemoteServerKind::DirectUrl: return "direct-url";
    case app::RemoteServerKind::InternetRadio: return "radio";
    case app::RemoteServerKind::PlaylistManifest: return "playlist";
    case app::RemoteServerKind::Hls:
    case app::RemoteServerKind::Dash:
        return "adaptive-stream";
    case app::RemoteServerKind::Smb:
    case app::RemoteServerKind::Nfs:
    case app::RemoteServerKind::WebDav:
    case app::RemoteServerKind::Ftp:
    case app::RemoteServerKind::Sftp:
        return "lan-share";
    case app::RemoteServerKind::DlnaUpnp: return "dlna-upnp";
    }
    return "unsupported";
}

bool remoteProviderHasCapability(const RemoteProviderManifest& manifest, RemoteProviderCapability capability) noexcept
{
    return std::find(manifest.capabilities.begin(), manifest.capabilities.end(), capability) != manifest.capabilities.end();
}

RemoteProviderManifest remoteProviderManifest(app::RemoteServerKind kind)
{
    RemoteProviderManifest manifest{};
    manifest.kind = kind;
    manifest.type_id = std::string(remoteServerKindId(kind));
    manifest.family = std::string(remoteProviderFamily(kind));
    manifest.capabilities = {
        RemoteProviderCapability::Probe,
        RemoteProviderCapability::SearchTracks,
        RemoteProviderCapability::RecentTracks,
        RemoteProviderCapability::ResolveStream,
        RemoteProviderCapability::ReadOnly,
    };

    switch (kind) {
    case app::RemoteServerKind::LocalRoot:
        manifest.display_name = "Local Folder";
        manifest.capabilities = {RemoteProviderCapability::BrowseCatalog, RemoteProviderCapability::ReadOnly};
        break;
    case app::RemoteServerKind::Jellyfin:
        manifest.display_name = "Jellyfin";
        manifest.capabilities.push_back(RemoteProviderCapability::BrowseCatalog);
        manifest.capabilities.push_back(RemoteProviderCapability::BrowseArtists);
        manifest.capabilities.push_back(RemoteProviderCapability::BrowseAlbums);
        manifest.capabilities.push_back(RemoteProviderCapability::BrowsePlaylists);
        manifest.capabilities.push_back(RemoteProviderCapability::BrowseFolders);
        manifest.capabilities.push_back(RemoteProviderCapability::BrowseGenres);
        manifest.capabilities.push_back(RemoteProviderCapability::Favorites);
        manifest.capabilities.push_back(RemoteProviderCapability::QualitySelection);
        manifest.capabilities.push_back(RemoteProviderCapability::TranscodingPolicy);
        break;
    case app::RemoteServerKind::OpenSubsonic:
        manifest.display_name = "OpenSubsonic";
        manifest.capabilities.push_back(RemoteProviderCapability::BrowseCatalog);
        manifest.capabilities.push_back(RemoteProviderCapability::BrowseArtists);
        manifest.capabilities.push_back(RemoteProviderCapability::BrowseAlbums);
        manifest.capabilities.push_back(RemoteProviderCapability::BrowsePlaylists);
        manifest.capabilities.push_back(RemoteProviderCapability::BrowseGenres);
        manifest.capabilities.push_back(RemoteProviderCapability::Favorites);
        break;
    case app::RemoteServerKind::Navidrome:
        manifest.display_name = "Navidrome";
        manifest.capabilities.push_back(RemoteProviderCapability::BrowseCatalog);
        manifest.capabilities.push_back(RemoteProviderCapability::BrowseArtists);
        manifest.capabilities.push_back(RemoteProviderCapability::BrowseAlbums);
        manifest.capabilities.push_back(RemoteProviderCapability::BrowsePlaylists);
        manifest.capabilities.push_back(RemoteProviderCapability::BrowseGenres);
        manifest.capabilities.push_back(RemoteProviderCapability::Favorites);
        break;
    case app::RemoteServerKind::Emby:
        manifest.display_name = "Emby";
        manifest.capabilities.push_back(RemoteProviderCapability::BrowseCatalog);
        manifest.capabilities.push_back(RemoteProviderCapability::BrowseArtists);
        manifest.capabilities.push_back(RemoteProviderCapability::BrowseAlbums);
        manifest.capabilities.push_back(RemoteProviderCapability::BrowsePlaylists);
        manifest.capabilities.push_back(RemoteProviderCapability::BrowseFolders);
        manifest.capabilities.push_back(RemoteProviderCapability::BrowseGenres);
        manifest.capabilities.push_back(RemoteProviderCapability::QualitySelection);
        manifest.capabilities.push_back(RemoteProviderCapability::TranscodingPolicy);
        break;
    case app::RemoteServerKind::DirectUrl:
        manifest.display_name = "Direct URL";
        manifest.capabilities = {RemoteProviderCapability::ResolveStream, RemoteProviderCapability::DirectUrlPlayback, RemoteProviderCapability::ReadOnly};
        break;
    case app::RemoteServerKind::InternetRadio:
        manifest.display_name = "Internet Radio";
        manifest.capabilities = {RemoteProviderCapability::ResolveStream, RemoteProviderCapability::InternetRadioPlayback, RemoteProviderCapability::ReadOnly};
        break;
    case app::RemoteServerKind::PlaylistManifest:
        manifest.display_name = "Playlist Manifest";
        manifest.capabilities = {RemoteProviderCapability::BrowseCatalog, RemoteProviderCapability::ResolveStream, RemoteProviderCapability::PlaylistManifestPlayback, RemoteProviderCapability::ReadOnly};
        break;
    case app::RemoteServerKind::Hls:
        manifest.display_name = "HLS";
        manifest.capabilities = {RemoteProviderCapability::ResolveStream, RemoteProviderCapability::AdaptiveStreaming, RemoteProviderCapability::QualitySelection, RemoteProviderCapability::ReadOnly};
        break;
    case app::RemoteServerKind::Dash:
        manifest.display_name = "DASH";
        manifest.capabilities = {RemoteProviderCapability::ResolveStream, RemoteProviderCapability::AdaptiveStreaming, RemoteProviderCapability::QualitySelection, RemoteProviderCapability::ReadOnly};
        break;
    case app::RemoteServerKind::Smb:
        manifest.display_name = "SMB";
        manifest.capabilities = {RemoteProviderCapability::BrowseCatalog, RemoteProviderCapability::BrowseFolders, RemoteProviderCapability::ResolveStream, RemoteProviderCapability::LanShareAccess, RemoteProviderCapability::ReadOnly};
        break;
    case app::RemoteServerKind::Nfs:
        manifest.display_name = "NFS";
        manifest.capabilities = {RemoteProviderCapability::BrowseCatalog, RemoteProviderCapability::BrowseFolders, RemoteProviderCapability::ResolveStream, RemoteProviderCapability::LanShareAccess, RemoteProviderCapability::ReadOnly};
        break;
    case app::RemoteServerKind::WebDav:
        manifest.display_name = "WebDAV";
        manifest.capabilities = {RemoteProviderCapability::BrowseCatalog, RemoteProviderCapability::BrowseFolders, RemoteProviderCapability::ResolveStream, RemoteProviderCapability::LanShareAccess, RemoteProviderCapability::ReadOnly};
        break;
    case app::RemoteServerKind::Ftp:
        manifest.display_name = "FTP";
        manifest.capabilities = {RemoteProviderCapability::BrowseCatalog, RemoteProviderCapability::BrowseFolders, RemoteProviderCapability::ResolveStream, RemoteProviderCapability::LanShareAccess, RemoteProviderCapability::ReadOnly};
        break;
    case app::RemoteServerKind::Sftp:
        manifest.display_name = "SFTP";
        manifest.capabilities = {RemoteProviderCapability::BrowseCatalog, RemoteProviderCapability::BrowseFolders, RemoteProviderCapability::ResolveStream, RemoteProviderCapability::LanShareAccess, RemoteProviderCapability::ReadOnly};
        break;
    case app::RemoteServerKind::DlnaUpnp:
        manifest.display_name = "DLNA / UPnP";
        manifest.capabilities = {RemoteProviderCapability::Probe, RemoteProviderCapability::BrowseCatalog, RemoteProviderCapability::ResolveStream, RemoteProviderCapability::DlnaDiscovery, RemoteProviderCapability::ReadOnly};
        break;
    }
    return manifest;
}

} // namespace lofibox::remote
