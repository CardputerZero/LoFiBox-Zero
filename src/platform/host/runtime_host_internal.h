// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <chrono>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "app/runtime_services.h"

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#elif defined(__linux__)
#include <sys/types.h>
#endif

namespace lofibox::platform::host::runtime_detail {
namespace fs = std::filesystem;

std::string trim(std::string value);
std::string asciiLower(std::string value);
std::optional<std::string> nonEmptyValue(std::string value);
std::string replaceAll(std::string value, std::string_view from, std::string_view to);
std::string pathUtf8String(const fs::path& path);
std::string urlEncode(std::string_view value);
std::string fallbackTitleFromPath(const fs::path& path);
std::string fallbackAlbumFromPath(const fs::path& path);
std::string fallbackArtistFromPath(const fs::path& path);
fs::path projectRoot();
fs::path tagWriterScriptPath();
fs::path remoteMediaToolScriptPath();
std::string jsonEscape(std::string_view value);
std::vector<fs::path> configuredMediaRoots();
bool pathEquivalentToAnyRoot(const fs::path& path, const std::vector<fs::path>& roots);
std::optional<bool> parseJsonBool(std::string_view json, std::string_view marker);
std::optional<int> parseJsonInt(std::string_view json, std::string_view marker);
bool hasCoreMetadata(const app::TrackMetadata& metadata);

struct FilenameMetadataGuess {
    std::string title{};
    std::string artist{};
};

std::vector<FilenameMetadataGuess> fallbackMetadataGuessesFromPath(const fs::path& path);

#if defined(_WIN32)
std::optional<fs::path> resolveExecutablePath(const wchar_t* env_name, const wchar_t* executable_name);
#elif defined(__linux__)
std::optional<fs::path> resolveExecutablePath(const char* env_name, const char* executable_name);
#endif

std::optional<std::string> captureProcessOutput(const fs::path& executable, const std::vector<std::string>& args);
bool runProcess(const fs::path& executable, const std::vector<std::string>& args);

struct RunningProcess {
#if defined(_WIN32)
    PROCESS_INFORMATION process_info{};
#elif defined(__linux__)
    pid_t pid{-1};
#endif
    bool active{false};
};

struct RunningPipeProcess {
#if defined(_WIN32)
    PROCESS_INFORMATION process_info{};
    HANDLE read_handle{nullptr};
#elif defined(__linux__)
    pid_t pid{-1};
    int read_fd{-1};
#endif
    bool active{false};
};

#if defined(__linux__)
struct RunningInputProcess {
    pid_t pid{-1};
    int write_fd{-1};
    bool active{false};
};
#endif

bool spawnAudioProcess(RunningProcess& process, const fs::path& executable, const std::vector<std::string>& args);
void stopAudioProcess(RunningProcess& process);
void pauseAudioProcess(RunningProcess& process);
void resumeAudioProcess(RunningProcess& process);
bool audioProcessRunning(RunningProcess& process);
bool audioProcessFinished(RunningProcess& process);
bool spawnPipeProcess(RunningPipeProcess& process, const fs::path& executable, const std::vector<std::string>& args);
int readPipeProcess(RunningPipeProcess& process, char* buffer, int max_bytes);
void stopPipeProcess(RunningPipeProcess& process);
void pausePipeProcess(RunningPipeProcess& process);
void resumePipeProcess(RunningPipeProcess& process);
bool pipeProcessRunning(RunningPipeProcess& process);
#if defined(__linux__)
bool spawnInputProcess(RunningInputProcess& process, const fs::path& executable, const std::vector<std::string>& args);
int writeInputProcess(RunningInputProcess& process, const char* buffer, int max_bytes);
void closeInputProcess(RunningInputProcess& process);
void stopInputProcess(RunningInputProcess& process);
void pauseInputProcess(RunningInputProcess& process);
void resumeInputProcess(RunningInputProcess& process);
bool inputProcessRunning(RunningInputProcess& process);
bool inputProcessFinished(RunningInputProcess& process);
#endif
bool probeConnectivity();
bool metadataCompatibleWithFilename(const fs::path& path, const app::TrackMetadata& metadata);

struct CacheEntry {
    app::TrackMetadata metadata{};
    app::TrackLyrics lyrics{};
    app::TrackIdentity identity{};
    std::string release_mbid{};
    std::string release_group_mbid{};
    bool online_metadata_attempted{false};
    bool online_artwork_attempted{false};
    bool online_lyrics_attempted{false};
    bool track_identity_attempted{false};
    int track_identity_version{0};
    int lyrics_lookup_version{0};
};

struct SharedRuntimeCache {
    fs::path root{};
    mutable std::unordered_map<std::string, CacheEntry> entries{};
    mutable std::chrono::steady_clock::time_point last_musicbrainz_request{};
    SharedRuntimeCache();
};

std::string cacheKeyForPath(const fs::path& path);
fs::path metadataCachePath(const SharedRuntimeCache& cache, std::string_view key);
fs::path artworkCachePath(const SharedRuntimeCache& cache, std::string_view key);
fs::path artworkMissingMarkerPath(const SharedRuntimeCache& cache, std::string_view key);
fs::path lyricsCachePath(const SharedRuntimeCache& cache, std::string_view key);
void storeMetadataCache(const SharedRuntimeCache& cache, std::string_view key, const CacheEntry& entry);
CacheEntry loadMetadataCache(const SharedRuntimeCache& cache, std::string_view key);
void storeLyricsCache(const SharedRuntimeCache& cache, std::string_view key, const app::TrackLyrics& lyrics);
app::TrackLyrics loadLyricsCache(const SharedRuntimeCache& cache, std::string_view key);

std::optional<std::string> extractJsonString(std::string_view json, std::string_view marker, std::size_t start = 0);
std::optional<std::string> parseJsonStringAt(std::string_view json, std::size_t quote_pos);
std::size_t findJsonBlock(std::string_view json, std::string_view marker);
void respectMusicBrainzRateLimit(SharedRuntimeCache& cache);
app::TrackMetadata parseMetadataOutput(const std::string& output);
std::string repairMetadataText(std::string value);
std::optional<std::string> extractLyricsText(std::string_view json, std::string_view field);
std::optional<fs::path> resolvePythonPath();
bool writeTagPayload(const fs::path& python_path, const fs::path& target_path, const app::TagWriteRequest& request);
std::optional<std::string> callRemoteMediaTool(const fs::path& python_path, const std::string& payload_json);

} // namespace lofibox::platform::host::runtime_detail
