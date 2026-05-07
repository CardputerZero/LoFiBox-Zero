# SPDX-License-Identifier: GPL-3.0-or-later
"""Emby provider plugin module."""

from pathlib import Path
import sys
from typing import Any, Dict

_SCRIPT_DIR = Path(__file__).resolve().parent
_SRC_ROOT = _SCRIPT_DIR.parent.parent.parent / "src"
if _SRC_ROOT.exists():
    sys.path.insert(0, str(_SRC_ROOT))

from remote.emby import emby_provider
from remote.common.media_contract import tracks_response, nodes_response, stream_response


_METHOD_SCHEMAS: Dict[str, tuple] = {
    "probe":          (("profile",),          ("profile",)),
    "search":         (("profile", "query"),  ("profile", "session", "query", "limit")),
    "recent":         (("profile",),          ("profile", "session", "limit")),
    "library_tracks": (("profile",),          ("profile", "session", "limit")),
    "browse":         (("profile",),          ("profile", "session", "parent", "limit")),
    "resolve":        (("profile", "track"),  ("profile", "session", "track")),
}


def _validate_params(method: str, params: Dict[str, Any]) -> None:
    schema = _METHOD_SCHEMAS.get(method)
    if schema is None:
        return
    required, known = schema
    missing = [key for key in required if key not in params]
    if missing:
        raise ValueError(f"{method}: missing required params: {missing}")
    unknown = [key for key in params if key not in known]
    if unknown:
        print(f"[provider_module] {method}: unknown params {unknown} - protocol drift?", file=sys.stderr)


def profile_schema() -> Dict[str, Any]:
    return {
        "fields": [
            {"name": "base_url", "type": "url", "required": True},
            {"name": "username", "type": "string", "required": True},
            {"name": "password", "type": "secret", "required": True},
            {"name": "api_token", "type": "string", "required": False},
        ]
    }


def dispatch(method: str, params: Dict[str, Any]):
    if method == "ping":
        return {"pong": True}
    if method == "profile_schema":
        return profile_schema()

    _validate_params(method, params)
    p = params

    if method == "probe":
        return emby_provider.probe(p["profile"])
    if method == "search":
        return tracks_response(p["profile"], emby_provider.search(
            p["profile"], p.get("session", {}), p["query"], int(p.get("limit", 20))))
    if method == "recent":
        return tracks_response(p["profile"], emby_provider.recent(
            p["profile"], p.get("session", {}), int(p.get("limit", 20))))
    if method == "library_tracks":
        return tracks_response(p["profile"], emby_provider.library_tracks(
            p["profile"], p.get("session", {}), int(p.get("limit", 50))))
    if method == "browse":
        return nodes_response(p["profile"], emby_provider.browse(
            p["profile"], p.get("session", {}), p.get("parent", {}), int(p.get("limit", 50))))
    if method == "resolve":
        return stream_response(p["profile"], emby_provider.resolve(
            p["profile"], p.get("session", {}), p["track"]))
    return None
