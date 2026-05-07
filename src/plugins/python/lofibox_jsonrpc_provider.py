#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later

"""
LoFiBox JSON-RPC Provider Framework.

Reads JSON-RPC 2.0 requests (one per line) from stdin,
dispatches to a provider module, writes JSON-RPC responses to stdout.

The provider module must implement these functions:
    profile_schema() -> dict
    probe(profile: dict) -> dict
    search(profile: dict, session: dict, query: str, limit: int) -> list[dict]
    recent(profile: dict, session: dict, limit: int) -> list[dict]
    library_tracks(profile: dict, session: dict, limit: int) -> list[dict]
    browse(profile: dict, session: dict, parent: dict, limit: int) -> list[dict]
    resolve(profile: dict, session: dict, track: dict) -> dict

Responses use the same format as the existing media_contract helpers:
    tracks_response(profile, tracks) -> {"tracks": [...]}
    nodes_response(profile, nodes) -> {"nodes": [...]}
    stream_response(profile, stream) -> {...}
"""

import json
import sys
import traceback
from pathlib import Path
from typing import Any, Callable, Dict, List, Optional


def _add_import_roots(plugin_dir: Path) -> None:
    source_root = plugin_dir.parent.parent.parent / "src"
    if source_root.exists():
        sys.path.insert(0, str(source_root))
    sys.path.insert(0, str(plugin_dir))


def _load_provider(plugin_dir: Path):
    provider_path = plugin_dir / "provider_module.py"
    if not provider_path.exists():
        raise FileNotFoundError(f"No provider_module.py in {plugin_dir}")

    import importlib.util
    spec = importlib.util.spec_from_file_location("provider_module", str(provider_path))
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def _method_dispatch(provider_module) -> Dict[str, Callable]:
    methods: Dict[str, Callable] = {}

    if hasattr(provider_module, "profile_schema"):
        methods["profile_schema"] = lambda params: provider_module.profile_schema()
    if hasattr(provider_module, "probe"):
        methods["probe"] = lambda params: provider_module.probe(params["profile"])
    if hasattr(provider_module, "search"):
        methods["search"] = lambda params: {
            "tracks": provider_module.search(
                params["profile"],
                params.get("session", {}),
                params["query"],
                int(params.get("limit", 20)))
        }
    if hasattr(provider_module, "recent"):
        methods["recent"] = lambda params: {
            "tracks": provider_module.recent(
                params["profile"],
                params.get("session", {}),
                int(params.get("limit", 20)))
        }
    if hasattr(provider_module, "library_tracks"):
        methods["library_tracks"] = lambda params: {
            "tracks": provider_module.library_tracks(
                params["profile"],
                params.get("session", {}),
                int(params.get("limit", 50)))
        }
    if hasattr(provider_module, "browse"):
        methods["browse"] = lambda params: {
            "nodes": provider_module.browse(
                params["profile"],
                params.get("session", {}),
                params.get("parent", {}),
                int(params.get("limit", 50)))
        }
    if hasattr(provider_module, "resolve"):
        methods["resolve"] = lambda params: provider_module.resolve(
            params["profile"],
            params.get("session", {}),
            params["track"])

    return methods


def _send_response(request_id: Any, result: Any) -> None:
    sys.stdout.write(json.dumps({
        "jsonrpc": "2.0",
        "result": result,
        "id": request_id,
    }, separators=(",", ":")))
    sys.stdout.write("\n")
    sys.stdout.flush()


def _send_error(request_id: Any, code: int, message: str) -> None:
    sys.stdout.write(json.dumps({
        "jsonrpc": "2.0",
        "error": {"code": code, "message": message},
        "id": request_id,
    }, separators=(",", ":")))
    sys.stdout.write("\n")
    sys.stdout.flush()


def _send_ready() -> None:
    sys.stdout.write(json.dumps({
        "jsonrpc": "2.0",
        "method": "ready",
        "params": {"status": "ok"},
    }, separators=(",", ":")))
    sys.stdout.write("\n")
    sys.stdout.flush()


def run_loop(plugin_dir: Path) -> int:
    provider = _load_provider(plugin_dir)
    methods = _method_dispatch(provider)
    methods["ping"] = lambda params: {"pong": True}
    _send_ready()

    for line in sys.stdin:
        line = line.strip()
        if not line:
            continue

        try:
            request = json.loads(line)
        except json.JSONDecodeError:
            _send_error(None, -32700, "Parse error")
            continue

        request_id = request.get("id")
        method_name = request.get("method", "")
        params = request.get("params", {})

        handler = methods.get(method_name)
        if handler is None:
            _send_error(request_id, -32601, f"Method not found: {method_name}")
            continue

        try:
            result = handler(params)
            _send_response(request_id, result)
        except Exception:
            tb = traceback.format_exc()
            _send_error(request_id, -32603, f"Internal error: {tb[-256:]}")

    return 0


def main() -> int:
    plugin_dir = Path(sys.argv[1]) if len(sys.argv) > 1 else Path.cwd()
    _add_import_roots(plugin_dir)
    return run_loop(plugin_dir)


if __name__ == "__main__":
    raise SystemExit(main())
