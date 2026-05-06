# SPDX-License-Identifier: GPL-3.0-or-later
"""OpenSubsonic provider plugin module, with no session params in calls."""

from pathlib import Path
import sys
from typing import Any, Dict

_SCRIPT_DIR = Path(__file__).resolve().parent
_SRC_ROOT = _SCRIPT_DIR.parent.parent.parent / "src"
if _SRC_ROOT.exists():
    sys.path.insert(0, str(_SRC_ROOT))

from remote.opensubsonic import opensubsonic_provider
from remote.common.media_contract import tracks_response, nodes_response, stream_response


_METHOD_SCHEMAS: Dict[str, tuple] = {
    "probe":          (("profile",),         ("profile",)),
    "search":         (("profile", "query"), ("profile", "query", "limit")),
    "recent":         (("profile",),         ("profile", "limit")),
    "library_tracks": (("profile",),         ("profile", "limit")),
    "browse":         (("profile",),         ("profile", "parent", "limit")),
    "resolve":        (("profile", "track"), ("profile", "track")),
}


def _validate_params(method: str, params: Dict[str, Any]) -> None:
    schema = _METHOD_SCHEMAS.get(method)
    if schema is None:
        return
    required, known = schema
    missing = [k for k in required if k not in params]
    if missing:
        raise ValueError(f"{method}: missing required params: {missing}")
    unknown = [k for k in params if k not in known]
    if unknown:
        import sys as _sys
        print(f"[provider_module] {method}: unknown params {unknown} - protocol drift?",
              file=_sys.stderr)


def profile_schema() -> Dict[str, Any]:
    return {
        "fields": [
            {"name": "base_url", "type": "url", "required": True},
            {"name": "username", "type": "string", "required": True},
            {"name": "password", "type": "secret", "required": True},
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
        return opensubsonic_provider.probe(p["profile"])
    if method == "search":
        return tracks_response(p["profile"], opensubsonic_provider.search(
            p["profile"], p["query"], int(p.get("limit", 20))))
    if method == "recent":
        return tracks_response(p["profile"], opensubsonic_provider.recent(
            p["profile"], int(p.get("limit", 20))))
    if method == "library_tracks":
        return tracks_response(p["profile"], opensubsonic_provider.library_tracks(
            p["profile"], int(p.get("limit", 50))))
    if method == "browse":
        return nodes_response(p["profile"], opensubsonic_provider.browse(
            p["profile"], p.get("parent", {}), int(p.get("limit", 50))))
    if method == "resolve":
        return stream_response(p["profile"], opensubsonic_provider.resolve(
            p["profile"], p["track"]))
    return None
