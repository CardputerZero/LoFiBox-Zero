# SPDX-License-Identifier: GPL-3.0-or-later

import urllib.parse
from typing import Any, Dict, List

from remote.jellyfin import jellyfin_provider
from remote.tooling.http_json import base_url


def profile_schema() -> Dict[str, Any]:
    return jellyfin_provider.profile_schema()


def probe(profile: Dict[str, Any]) -> Dict[str, Any]:
    result = jellyfin_provider.probe(profile)
    if result.get("server_name") == "Jellyfin":
        result["server_name"] = "Emby"
    return result


def search(profile: Dict[str, Any], session: Dict[str, Any], query: str, limit: int) -> List[Dict[str, Any]]:
    return jellyfin_provider.search(profile, session, query, limit)


def recent(profile: Dict[str, Any], session: Dict[str, Any], limit: int) -> List[Dict[str, Any]]:
    return jellyfin_provider.recent(profile, session, limit)


def library_tracks(profile: Dict[str, Any], session: Dict[str, Any], limit: int) -> List[Dict[str, Any]]:
    return jellyfin_provider.library_tracks(profile, session, limit)


def browse(profile: Dict[str, Any], session: Dict[str, Any], parent: Dict[str, Any], limit: int) -> List[Dict[str, Any]]:
    return jellyfin_provider.browse(profile, session, parent, limit)


def resolve(profile: Dict[str, Any], session: Dict[str, Any], track: Dict[str, Any]) -> Dict[str, Any]:
    token = session.get("access_token", "")
    user_id = session.get("user_id", "")
    detail = jellyfin_provider.item_detail(profile, session, track.get("id", ""))
    return {
        "url": f"{base_url(profile)}/Audio/{track['id']}/stream.mp3?static=true&UserId={urllib.parse.quote(user_id)}&api_key={urllib.parse.quote(token)}",
        "headers": [],
        "seekable": True,
        **jellyfin_provider.stream_diagnostics(detail),
    }
