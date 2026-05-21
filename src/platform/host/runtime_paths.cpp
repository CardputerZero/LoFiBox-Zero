// SPDX-License-Identifier: GPL-3.0-or-later

#include "platform/host/runtime_paths.h"

#include <cstdlib>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace lofibox::platform::host::runtime_paths {
namespace {

constexpr const char* kAppDirName = "lofibox";

#if defined(_WIN32)
fs::path knownEnvPath(const char* name)
{
    char* raw_value = nullptr;
    std::size_t value_length = 0;
    if (_dupenv_s(&raw_value, &value_length, name) == 0 && raw_value != nullptr && *raw_value != '\0') {
        fs::path result{raw_value};
        std::free(raw_value);
        return result;
    }
    std::free(raw_value);
    return {};
}
#else
#include <pwd.h>
#include <sys/types.h>
#include <unistd.h>

fs::path envPath(const char* name)
{
    if (const char* value = std::getenv(name); value != nullptr && *value != '\0') {
        return fs::path{value};
    }
    return {};
}

fs::path homeDir()
{
    if (auto home = envPath("HOME"); !home.empty()) {
        return home;
    }
    const auto* entry = getpwuid(geteuid());
    if (entry == nullptr || entry->pw_dir == nullptr || *entry->pw_dir == '\0') {
        return {};
    }
    return fs::path{entry->pw_dir};
}
#endif

fs::path appendAppDir(fs::path root)
{
    return root.empty() ? root : root / kAppDirName;
}

} // namespace

fs::path configHome()
{
#if defined(_WIN32)
    fs::path root = knownEnvPath("APPDATA");
    return root.empty() ? fs::temp_directory_path() : root;
#else
    if (auto root = envPath("XDG_CONFIG_HOME"); !root.empty()) {
        return root;
    }
    if (auto home = homeDir(); !home.empty()) {
        return home / ".config";
    }
    return fs::temp_directory_path();
#endif
}

fs::path dataHome()
{
#if defined(_WIN32)
    fs::path root = knownEnvPath("LOCALAPPDATA");
    return root.empty() ? fs::temp_directory_path() : root;
#else
    if (auto root = envPath("XDG_DATA_HOME"); !root.empty()) {
        return root;
    }
    if (auto home = homeDir(); !home.empty()) {
        return home / ".local" / "share";
    }
    return fs::temp_directory_path();
#endif
}

fs::path cacheHome()
{
#if defined(_WIN32)
    fs::path root = knownEnvPath("LOCALAPPDATA");
    return root.empty() ? fs::temp_directory_path() : root;
#else
    if (auto root = envPath("XDG_CACHE_HOME"); !root.empty()) {
        return root;
    }
    if (auto home = homeDir(); !home.empty()) {
        return home / ".cache";
    }
    return fs::temp_directory_path();
#endif
}

fs::path stateHome()
{
#if defined(_WIN32)
    fs::path root = knownEnvPath("LOCALAPPDATA");
    return root.empty() ? fs::temp_directory_path() : root;
#else
    if (auto root = envPath("XDG_STATE_HOME"); !root.empty()) {
        return root;
    }
    if (auto home = homeDir(); !home.empty()) {
        return home / ".local" / "state";
    }
    return fs::temp_directory_path();
#endif
}

fs::path runtimeHome()
{
#if defined(_WIN32)
    return fs::temp_directory_path();
#else
    if (auto root = envPath("XDG_RUNTIME_DIR"); !root.empty()) {
        return root;
    }
    return fs::temp_directory_path();
#endif
}

fs::path temporaryHome()
{
    return fs::temp_directory_path();
}

fs::path appConfigDir()
{
    return appendAppDir(configHome());
}

fs::path appDataDir()
{
    return appendAppDir(dataHome());
}

fs::path appCacheDir()
{
    return appendAppDir(cacheHome());
}

fs::path appStateDir()
{
    return appendAppDir(stateHome());
}

fs::path appRuntimeDir()
{
    return appendAppDir(runtimeHome());
}

fs::path appTemporaryDir()
{
    return temporaryHome() / kAppDirName;
}

fs::path runtimeLogPath()
{
#if defined(_WIN32)
    if (auto explicit_path = knownEnvPath("LOFIBOX_RUNTIME_LOG_PATH"); !explicit_path.empty()) {
        return explicit_path;
    }
#else
    if (auto explicit_path = envPath("LOFIBOX_RUNTIME_LOG_PATH"); !explicit_path.empty()) {
        return explicit_path;
    }
#endif
    return appStateDir() / "runtime.log";
}

} // namespace lofibox::platform::host::runtime_paths
