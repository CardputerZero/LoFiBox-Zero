// SPDX-License-Identifier: GPL-3.0-or-later

#include "app/source_manager_projection.h"

namespace lofibox::app {
namespace {

std::string kindLabel(RemoteServerKind kind)
{
    switch (kind) {
    case RemoteServerKind::LocalRoot: return "LOCAL";
    case RemoteServerKind::Jellyfin: return "JELLYFIN";
    case RemoteServerKind::OpenSubsonic: return "OPENSUBSONIC";
    case RemoteServerKind::Navidrome: return "NAVIDROME";
    case RemoteServerKind::Emby: return "EMBY";
    case RemoteServerKind::DirectUrl: return "URL";
    case RemoteServerKind::InternetRadio: return "RADIO";
    case RemoteServerKind::PlaylistManifest: return "PLAYLIST";
    case RemoteServerKind::Hls: return "HLS";
    case RemoteServerKind::Dash: return "DASH";
    case RemoteServerKind::Smb: return "SMB";
    case RemoteServerKind::Nfs: return "NFS";
    case RemoteServerKind::WebDav: return "WEBDAV";
    case RemoteServerKind::Ftp: return "FTP";
    case RemoteServerKind::Sftp: return "SFTP";
    case RemoteServerKind::DlnaUpnp: return "DLNA";
    }
    return "REMOTE";
}

} // namespace

std::vector<std::pair<std::string, std::string>> buildSourceManagerRows(const std::vector<RemoteServerProfile>& profiles)
{
    return buildSourceManagerRows(profiles, {});
}

std::vector<std::pair<std::string, std::string>> buildSourceManagerRows(
    const std::vector<RemoteServerProfile>& profiles,
    const std::vector<remote::RemoteProviderManifest>& manifests)
{
    std::vector<std::pair<std::string, std::string>> rows{};
    rows.emplace_back("LOCAL LIBRARY", "FILES");
    rows.emplace_back("DIRECT URL", "STREAM");
    rows.emplace_back("RADIO", "STATIONS");
    for (const auto& manifest : manifests) {
        rows.emplace_back("ADD " + manifest.display_name, manifest.family);
    }
    for (const auto& profile : profiles) {
        if (profile.kind == RemoteServerKind::LocalRoot) {
            rows.emplace_back(
                profile.name.empty() ? kindLabel(profile.kind) : profile.name,
                profile.enabled ? "LOCAL ROOT" : "DISABLED");
            continue;
        }
        const bool has_endpoint = !profile.base_url.empty();
        const bool has_secret = !profile.password.empty() || !profile.api_token.empty();
        rows.emplace_back(
            profile.name.empty() ? kindLabel(profile.kind) : profile.name,
            has_endpoint && has_secret ? "CRED OK" : (has_endpoint ? "NEEDS CRED" : kindLabel(profile.kind)));
    }
    return rows;
}

} // namespace lofibox::app
