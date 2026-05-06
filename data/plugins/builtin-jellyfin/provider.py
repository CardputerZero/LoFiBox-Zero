#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""LoFiBox JSON-RPC provider entry point. Calls provider_module.dispatch()."""

import json
import sys
import traceback
from pathlib import Path
from typing import Any

_PLUGIN_DIR = Path(__file__).resolve().parent


def _setup_paths() -> None:
    src_root = _PLUGIN_DIR.parent.parent.parent / "src"
    if src_root.exists():
        sys.path.insert(0, str(src_root))


def _load_module():
    import importlib.util
    mod_path = _PLUGIN_DIR / "provider_module.py"
    spec = importlib.util.spec_from_file_location("provider_module", str(mod_path))
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


def _respond(request_id: Any, result: Any) -> None:
    sys.stdout.write(json.dumps({"jsonrpc": "2.0", "result": result, "id": request_id}, separators=(",", ":")))
    sys.stdout.write("\n")
    sys.stdout.flush()


def _notify_ready() -> None:
    sys.stdout.write(json.dumps({"jsonrpc": "2.0", "method": "ready", "params": {"status": "ok"}}, separators=(",", ":")))
    sys.stdout.write("\n")
    sys.stdout.flush()


def _error(request_id: Any, code: int, message: str) -> None:
    sys.stdout.write(json.dumps({"jsonrpc": "2.0", "error": {"code": code, "message": message}, "id": request_id}, separators=(",", ":")))
    sys.stdout.write("\n")
    sys.stdout.flush()


def main() -> int:
    _setup_paths()
    provider = _load_module()
    dispatch = getattr(provider, "dispatch", None)
    if dispatch is None:
        _error(None, -32600, "No dispatch() in provider_module")
        return 1
    _notify_ready()

    for line in sys.stdin:
        line = line.strip()
        if not line:
            continue
        try:
            req = json.loads(line)
        except json.JSONDecodeError:
            _error(None, -32700, "Parse error")
            continue

        rid = req.get("id")
        method = req.get("method", "")
        params = req.get("params", {})

        try:
            result = dispatch(method, params)
            if result is None:
                _error(rid, -32601, f"Method not found: {method}")
            else:
                _respond(rid, result)
        except Exception:
            tb = traceback.format_exc()
            _error(rid, -32603, tb[-256:])

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
