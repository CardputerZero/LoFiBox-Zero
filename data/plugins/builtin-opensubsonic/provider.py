#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""LoFiBox JSON-RPC provider entry point. Calls provider_module.dispatch()."""

import json, sys, traceback
from pathlib import Path
from typing import Any

_PLUGIN_DIR = Path(__file__).resolve().parent

def _setup_paths():
    src_root = _PLUGIN_DIR.parent.parent.parent / "src"
    if src_root.exists(): sys.path.insert(0, str(src_root))

def _load_module():
    import importlib.util
    mod_path = _PLUGIN_DIR / "provider_module.py"
    spec = importlib.util.spec_from_file_location("provider_module", str(mod_path))
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod

def _respond(rid, result):
    sys.stdout.write(json.dumps({"jsonrpc":"2.0","result":result,"id":rid}, separators=(",",":")))
    sys.stdout.write("\n"); sys.stdout.flush()

def _error(rid, code, msg):
    sys.stdout.write(json.dumps({"jsonrpc":"2.0","error":{"code":code,"message":msg},"id":rid}, separators=(",",":")))
    sys.stdout.write("\n"); sys.stdout.flush()

def _ready():
    sys.stdout.write(json.dumps({"jsonrpc":"2.0","method":"ready","params":{"status":"ok"}}, separators=(",",":")))
    sys.stdout.write("\n"); sys.stdout.flush()

def main():
    _setup_paths(); provider = _load_module()
    dispatch = getattr(provider, "dispatch", None)
    if dispatch is None: _error(None, -32600, "No dispatch()"); return 1
    _ready()
    for line in sys.stdin:
        line = line.strip()
        if not line: continue
        try: req = json.loads(line)
        except json.JSONDecodeError: _error(None, -32700, "Parse error"); continue
        rid, method, params = req.get("id"), req.get("method",""), req.get("params",{})
        try:
            result = dispatch(method, params)
            _respond(rid, result) if result is not None else _error(rid, -32601, f"Method not found: {method}")
        except Exception: _error(rid, -32603, traceback.format_exc()[-256:])
    return 0

if __name__ == "__main__": raise SystemExit(main())
