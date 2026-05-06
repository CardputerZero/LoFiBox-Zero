# SPDX-License-Identifier: GPL-3.0-or-later

import json
import sys
from typing import Any, Dict, List

from remote.emby import emby_provider
from remote.jellyfin import jellyfin_provider
from remote.common.media_contract import nodes_response, stream_response, tracks_response
from remote.dlna import dlna_provider
from remote.navidrome import navidrome_provider
from remote.opensubsonic import opensubsonic_provider
from remote.playlist import playlist_provider
from remote.shares import share_provider


def _provider(kind: str):
    if kind == "jellyfin":
        return jellyfin_provider
    if kind == "emby":
        return emby_provider
    if kind == "navidrome":
        return navidrome_provider
    if kind == "opensubsonic":
        return opensubsonic_provider
    if kind == "playlist-manifest":
        return playlist_provider
    if kind in ("webdav", "ftp", "sftp", "smb", "nfs"):
        return share_provider
    if kind == "dlna-upnp":
        return dlna_provider
    raise ValueError(f"Unsupported kind: {kind}")


def _normalized_kind(profile: Dict[str, Any]) -> str:
    kind = profile["kind"]
    return "navidrome" if kind == "navidrome" else kind


def _probe(payload: Dict[str, Any]) -> Dict[str, Any]:
    profile = payload["profile"]
    return _provider(_normalized_kind(profile)).probe(profile)


def _search(payload: Dict[str, Any]) -> Dict[str, Any]:
    profile = payload["profile"]
    session = payload["session"]
    query = payload["query"]
    limit = int(payload.get("limit", 20))
    kind = _normalized_kind(profile)
    if kind in ("opensubsonic", "navidrome"):
        return tracks_response(profile, _provider(kind).search(profile, query, limit))
    return tracks_response(profile, _provider(kind).search(profile, session, query, limit))


def _recent(payload: Dict[str, Any]) -> Dict[str, Any]:
    profile = payload["profile"]
    session = payload["session"]
    limit = int(payload.get("limit", 20))
    kind = _normalized_kind(profile)
    if kind in ("opensubsonic", "navidrome"):
        return tracks_response(profile, _provider(kind).recent(profile, limit))
    return tracks_response(profile, _provider(kind).recent(profile, session, limit))


def _library_tracks(payload: Dict[str, Any]) -> Dict[str, Any]:
    profile = payload["profile"]
    session = payload["session"]
    limit = int(payload.get("limit", 50))
    kind = _normalized_kind(profile)
    if kind in ("opensubsonic", "navidrome"):
        return tracks_response(profile, _provider(kind).library_tracks(profile, limit))
    return tracks_response(profile, _provider(kind).library_tracks(profile, session, limit))


def _browse(payload: Dict[str, Any]) -> Dict[str, Any]:
    profile = payload["profile"]
    session = payload["session"]
    parent = payload["parent"]
    limit = int(payload.get("limit", 50))
    kind = _normalized_kind(profile)
    if kind in ("opensubsonic", "navidrome"):
        return nodes_response(profile, _provider(kind).browse(profile, parent, limit))
    return nodes_response(profile, _provider(kind).browse(profile, session, parent, limit))


def _resolve(payload: Dict[str, Any]) -> Dict[str, Any]:
    profile = payload["profile"]
    session = payload["session"]
    track = payload["track"]
    kind = _normalized_kind(profile)
    if kind in ("opensubsonic", "navidrome"):
        return stream_response(profile, _provider(kind).resolve(profile, track))
    return stream_response(profile, _provider(kind).resolve(profile, session, track))


def _profile_schema(payload: Dict[str, Any]) -> Dict[str, Any]:
    profile = payload["profile"]
    kind = _normalized_kind(profile)
    return _provider(kind).profile_schema()


def run_payload(payload: Dict[str, Any]) -> Dict[str, Any]:
    action = payload["action"]
    if action == "profile_schema":
        return _profile_schema(payload)
    if action == "probe":
        return _probe(payload)
    if action == "search":
        return _search(payload)
    if action == "recent":
        return _recent(payload)
    if action == "library_tracks":
        return _library_tracks(payload)
    if action == "browse":
        return _browse(payload)
    if action == "resolve":
        return _resolve(payload)
    raise ValueError(f"Unsupported action: {action}")


def main(argv: List[str]) -> int:
    if len(argv) != 2:
        return 2
    with open(argv[1], "r", encoding="utf-8-sig") as handle:
        payload = json.load(handle)
    sys.stdout.write(json.dumps(run_payload(payload), separators=(",", ":")))
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
