// SPDX-License-Identifier: GPL-3.0-or-later

#include "app/remote_profile_store.h"

#include <algorithm>
#include <cctype>

namespace lofibox::app {

std::string remoteServerKindToString(RemoteServerKind kind)
{
    switch (kind) {
    case RemoteServerKind::LocalRoot: return "local-root";
    case RemoteServerKind::Jellyfin: return "jellyfin";
    case RemoteServerKind::OpenSubsonic: return "opensubsonic";
    case RemoteServerKind::Navidrome: return "navidrome";
    case RemoteServerKind::Emby: return "emby";
    case RemoteServerKind::DirectUrl: return "direct-url";
    case RemoteServerKind::InternetRadio: return "internet-radio";
    case RemoteServerKind::PlaylistManifest: return "playlist-manifest";
    case RemoteServerKind::Hls: return "hls";
    case RemoteServerKind::Dash: return "dash";
    case RemoteServerKind::Smb: return "smb";
    case RemoteServerKind::Nfs: return "nfs";
    case RemoteServerKind::WebDav: return "webdav";
    case RemoteServerKind::Ftp: return "ftp";
    case RemoteServerKind::Sftp: return "sftp";
    case RemoteServerKind::DlnaUpnp: return "dlna-upnp";
    }
    return "jellyfin";
}

RemoteServerKind remoteServerKindFromString(const std::string& value)
{
    std::string lower = value;
    std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    if (lower == "opensubsonic" || lower == "subsonic") {
        return RemoteServerKind::OpenSubsonic;
    }
    if (lower == "local-root" || lower == "local_media_root" || lower == "localroot" || lower == "local") {
        return RemoteServerKind::LocalRoot;
    }
    if (lower == "navidrome") {
        return RemoteServerKind::Navidrome;
    }
    if (lower == "emby") {
        return RemoteServerKind::Emby;
    }
    if (lower == "direct-url" || lower == "url" || lower == "http" || lower == "https") {
        return RemoteServerKind::DirectUrl;
    }
    if (lower == "internet-radio" || lower == "radio" || lower == "station") {
        return RemoteServerKind::InternetRadio;
    }
    if (lower == "playlist-manifest" || lower == "playlist" || lower == "m3u" || lower == "pls" || lower == "xspf") {
        return RemoteServerKind::PlaylistManifest;
    }
    if (lower == "hls") {
        return RemoteServerKind::Hls;
    }
    if (lower == "dash") {
        return RemoteServerKind::Dash;
    }
    if (lower == "smb") {
        return RemoteServerKind::Smb;
    }
    if (lower == "nfs") {
        return RemoteServerKind::Nfs;
    }
    if (lower == "webdav" || lower == "web-dav") {
        return RemoteServerKind::WebDav;
    }
    if (lower == "ftp") {
        return RemoteServerKind::Ftp;
    }
    if (lower == "sftp") {
        return RemoteServerKind::Sftp;
    }
    if (lower == "dlna" || lower == "upnp" || lower == "dlna-upnp") {
        return RemoteServerKind::DlnaUpnp;
    }
    return RemoteServerKind::Jellyfin;
}

RemoteServerProfile sanitizeRemoteProfileForPersistence(RemoteServerProfile profile)
{
    profile.password.clear();
    profile.api_token.clear();
    return profile;
}

} // namespace lofibox::app
