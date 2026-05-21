// SPDX-License-Identifier: GPL-3.0-or-later

#include "remote/common/remote_source_registry.h"

namespace lofibox::remote {

bool RemoteSourceRegistry::supported(app::RemoteServerKind kind) const noexcept
{
    switch (kind) {
    case app::RemoteServerKind::LocalRoot:
    case app::RemoteServerKind::Jellyfin:
    case app::RemoteServerKind::OpenSubsonic:
    case app::RemoteServerKind::Navidrome:
    case app::RemoteServerKind::Emby:
    case app::RemoteServerKind::DirectUrl:
    case app::RemoteServerKind::InternetRadio:
    case app::RemoteServerKind::PlaylistManifest:
    case app::RemoteServerKind::Hls:
    case app::RemoteServerKind::Dash:
    case app::RemoteServerKind::Smb:
    case app::RemoteServerKind::Nfs:
    case app::RemoteServerKind::WebDav:
    case app::RemoteServerKind::Ftp:
    case app::RemoteServerKind::Sftp:
    case app::RemoteServerKind::DlnaUpnp:
        return true;
    }
    return false;
}

bool RemoteSourceRegistry::openSubsonicCompatible(app::RemoteServerKind kind) const noexcept
{
    return kind == app::RemoteServerKind::OpenSubsonic || kind == app::RemoteServerKind::Navidrome;
}

std::string_view RemoteSourceRegistry::providerFamily(app::RemoteServerKind kind) const noexcept
{
    return remoteProviderFamily(kind);
}

RemoteProviderManifest RemoteSourceRegistry::manifest(app::RemoteServerKind kind) const
{
    return remoteProviderManifest(kind);
}

std::vector<RemoteProviderManifest> RemoteSourceRegistry::manifests() const
{
    return {
        manifest(app::RemoteServerKind::Jellyfin),
        manifest(app::RemoteServerKind::OpenSubsonic),
        manifest(app::RemoteServerKind::Navidrome),
        manifest(app::RemoteServerKind::Emby),
        manifest(app::RemoteServerKind::DirectUrl),
        manifest(app::RemoteServerKind::InternetRadio),
        manifest(app::RemoteServerKind::PlaylistManifest),
        manifest(app::RemoteServerKind::Hls),
        manifest(app::RemoteServerKind::Dash),
        manifest(app::RemoteServerKind::Smb),
        manifest(app::RemoteServerKind::Nfs),
        manifest(app::RemoteServerKind::WebDav),
        manifest(app::RemoteServerKind::Ftp),
        manifest(app::RemoteServerKind::Sftp),
        manifest(app::RemoteServerKind::DlnaUpnp),
    };
}

} // namespace lofibox::remote
