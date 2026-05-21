// SPDX-License-Identifier: GPL-3.0-or-later

#include "platform/host/runtime_paths.h"

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>

#if defined(_WIN32)
int main()
{
    std::cout << "XDG path smoke is Linux-specific; skipping on this platform.\n";
    return 0;
}
#else
#include <pwd.h>
#include <sys/types.h>
#include <unistd.h>

namespace fs = std::filesystem;
namespace runtime_paths = lofibox::platform::host::runtime_paths;

namespace {

bool expectPath(const fs::path& actual, const fs::path& expected, const char* label)
{
    if (actual != expected) {
        std::cerr << label << " mismatch.\nExpected: " << expected << "\nActual: " << actual << '\n';
        return false;
    }
    return true;
}

fs::path accountHomeDir()
{
    const auto* entry = getpwuid(geteuid());
    if (entry == nullptr || entry->pw_dir == nullptr || *entry->pw_dir == '\0') {
        return {};
    }
    return entry->pw_dir;
}

} // namespace

int main()
{
    if (!expectPath(runtime_paths::appConfigDir(), fs::path{"/tmp/lofibox-xdg-config"} / "lofibox", "config")) {
        return 1;
    }
    if (!expectPath(runtime_paths::appDataDir(), fs::path{"/tmp/lofibox-xdg-data"} / "lofibox", "data")) {
        return 1;
    }
    if (!expectPath(runtime_paths::appCacheDir(), fs::path{"/tmp/lofibox-xdg-cache"} / "lofibox", "cache")) {
        return 1;
    }
    if (!expectPath(runtime_paths::appStateDir(), fs::path{"/tmp/lofibox-xdg-state"} / "lofibox", "state")) {
        return 1;
    }
    if (!expectPath(runtime_paths::appRuntimeDir(), fs::path{"/tmp/lofibox-xdg-runtime"} / "lofibox", "runtime")) {
        return 1;
    }
    if (!expectPath(runtime_paths::runtimeLogPath(), fs::path{"/tmp/lofibox-xdg-state"} / "lofibox" / "runtime.log", "runtime log")) {
        return 1;
    }

    unsetenv("XDG_CONFIG_HOME");
    unsetenv("XDG_DATA_HOME");
    unsetenv("XDG_CACHE_HOME");
    unsetenv("XDG_STATE_HOME");
    unsetenv("HOME");

    const auto account_home = accountHomeDir();
    if (account_home.empty()) {
        std::cerr << "Expected an account home from getpwuid(geteuid()).\n";
        return 1;
    }
    if (!expectPath(runtime_paths::appConfigDir(), account_home / ".config" / "lofibox", "fallback config")) {
        return 1;
    }
    if (!expectPath(runtime_paths::appDataDir(), account_home / ".local" / "share" / "lofibox", "fallback data")) {
        return 1;
    }
    if (!expectPath(runtime_paths::appCacheDir(), account_home / ".cache" / "lofibox", "fallback cache")) {
        return 1;
    }
    if (!expectPath(runtime_paths::appStateDir(), account_home / ".local" / "state" / "lofibox", "fallback state")) {
        return 1;
    }
    return 0;
}
#endif
