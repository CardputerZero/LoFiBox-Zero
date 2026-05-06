// SPDX-License-Identifier: GPL-3.0-or-later

#include "plugins/plugin_runtime.h"

#include <cassert>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace fs = std::filesystem;

namespace {

bool writeText(const fs::path& path, const std::string& text)
{
    std::error_code ec{};
    fs::create_directories(path.parent_path(), ec);
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out << text;
    return out.good();
}

bool python3Available()
{
#if defined(__linux__)
    return std::system("python3 --version >/dev/null 2>&1") == 0;
#else
    return false;
#endif
}

} // namespace

int main()
{
#if !defined(__linux__)
    std::cout << "plugin runtime smoke skipped: external helper runtime is linux-only\n";
    return 0;
#else
    if (!python3Available()) {
        std::cout << "plugin runtime smoke skipped: python3 unavailable\n";
        return 0;
    }

    const auto root = fs::temp_directory_path() / "lofibox-plugin-runtime-smoke";
    std::error_code ec{};
    fs::remove_all(root, ec);
    fs::create_directories(root, ec);

    const char* provider = R"PY(
import json
import sys

def send(payload):
    sys.stdout.write(json.dumps(payload, separators=(",", ":")) + "\n")
    sys.stdout.flush()

send({"jsonrpc": "2.0", "method": "ready", "params": {}})

for line in sys.stdin:
    try:
        request = json.loads(line)
    except Exception:
        continue
    request_id = request.get("id")
    method = request.get("method")
    if method == "echo":
        send({"jsonrpc": "2.0", "result": request.get("params", {}), "id": request_id})
    elif method == "ping":
        send({"jsonrpc": "2.0", "result": {"ok": True}, "id": request_id})
    else:
        send({"jsonrpc": "2.0", "error": {"code": -32601, "message": "unknown method"}, "id": request_id})
)PY";

    assert(writeText(root / "provider.py", provider));

    lofibox::plugins::PluginRuntimeConfig config{};
    config.plugin_dir = root;
    config.stderr_log_dir = root / "logs";
    config.command = "python3";
    config.args = {"provider.py"};
    config.ready_timeout = std::chrono::milliseconds{5000};
    config.request_timeout = std::chrono::milliseconds{5000};

    lofibox::plugins::PluginRuntime runtime{config};
    if (!runtime.start()) {
        std::cerr << "Plugin runtime failed to start: " << runtime.lastError() << "\n";
        return 1;
    }

    const auto echo = runtime.call("echo", R"({"value":"ok"})");
    assert(echo);
    assert(echo->find(R"("value":"ok")") != std::string::npos);
    assert(runtime.isHealthy());

    const auto ping = runtime.call("ping", "{}");
    assert(ping);
    assert(ping->find(R"("ok":true)") != std::string::npos);

    runtime.stop();
    fs::remove_all(root, ec);
    std::cout << "plugin runtime smoke passed\n";
    return 0;
#endif
}
