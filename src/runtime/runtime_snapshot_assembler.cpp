// SPDX-License-Identifier: GPL-3.0-or-later

#include "runtime/runtime_snapshot_assembler.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>

namespace lofibox::runtime {
namespace {

std::string trim(std::string value)
{
    const auto not_space = [](unsigned char ch) { return std::isspace(ch) == 0; };
    value.erase(value.begin(), std::find_if(value.begin(), value.end(), not_space));
    value.erase(std::find_if(value.rbegin(), value.rend(), not_space).base(), value.end());
    return value;
}

std::optional<double> parseLrcTimestamp(std::string_view line, std::size_t& tag_end)
{
    tag_end = 0;
    if (line.size() < 7 || line.front() != '[') {
        return std::nullopt;
    }
    const auto close = line.find(']');
    const auto colon = line.find(':');
    if (close == std::string_view::npos || colon == std::string_view::npos || colon > close) {
        return std::nullopt;
    }
    try {
        const auto minutes = std::stod(std::string{line.substr(1, colon - 1)});
        const auto seconds = std::stod(std::string{line.substr(colon + 1, close - colon - 1)});
        tag_end = close + 1;
        return (minutes * 60.0) + seconds;
    } catch (...) {
        return std::nullopt;
    }
}

bool lrcMetadataLine(std::string_view line) noexcept
{
    if (line.size() < 4 || line.front() != '[') {
        return false;
    }
    const auto close = line.find(']');
    const auto colon = line.find(':');
    return close != std::string_view::npos && colon != std::string_view::npos && colon < close;
}

std::vector<RuntimeLyricLine> lyricLinesFromText(std::string_view text)
{
    std::vector<RuntimeLyricLine> lines{};
    std::istringstream input{std::string{text}};
    std::string line{};
    int index = 0;
    while (std::getline(input, line)) {
        line = trim(std::move(line));
        if (line.empty()) {
            continue;
        }
        RuntimeLyricLine display{};
        display.index = index++;
        std::size_t tag_end = 0;
        if (const auto timestamp = parseLrcTimestamp(line, tag_end)) {
            display.timestamp_seconds = *timestamp;
            display.text = trim(line.substr(tag_end));
        } else if (lrcMetadataLine(line)) {
            --index;
            continue;
        } else {
            display.timestamp_seconds = -1.0;
            display.text = std::move(line);
        }
        if (!display.text.empty()) {
            lines.push_back(std::move(display));
        }
    }
    return lines;
}

int activeLyricIndex(const std::vector<RuntimeLyricLine>& lines, double elapsed_seconds, int duration_seconds)
{
    if (lines.empty()) {
        return -1;
    }
    const bool has_timestamps = std::any_of(lines.begin(), lines.end(), [](const RuntimeLyricLine& line) {
        return line.timestamp_seconds >= 0.0;
    });
    if (has_timestamps) {
        int active = 0;
        for (int index = 0; index < static_cast<int>(lines.size()); ++index) {
            if (lines[static_cast<std::size_t>(index)].timestamp_seconds >= 0.0
                && lines[static_cast<std::size_t>(index)].timestamp_seconds <= elapsed_seconds) {
                active = index;
            }
        }
        return active;
    }
    const double progress = std::clamp(elapsed_seconds / static_cast<double>(std::max(1, duration_seconds)), 0.0, 0.999);
    return std::clamp(static_cast<int>(progress * static_cast<double>(lines.size())), 0, static_cast<int>(lines.size()) - 1);
}

std::string libraryStateLabel(app::LibraryIndexState state)
{
    switch (state) {
    case app::LibraryIndexState::Uninitialized: return "UNINITIALIZED";
    case app::LibraryIndexState::Loading: return "LOADING";
    case app::LibraryIndexState::Ready: return "READY";
    case app::LibraryIndexState::Degraded: return "DEGRADED";
    }
    return "UNKNOWN";
}

bool capabilityAvailable(const application::RuntimeDiagnosticSnapshot& diagnostics, std::string_view name)
{
    const auto found = std::find_if(diagnostics.capabilities.begin(), diagnostics.capabilities.end(), [name](const auto& capability) {
        return capability.name == name;
    });
    return found == diagnostics.capabilities.end() || found->available;
}

std::string capabilityDetail(const application::RuntimeDiagnosticSnapshot& diagnostics, std::string_view name)
{
    const auto found = std::find_if(diagnostics.capabilities.begin(), diagnostics.capabilities.end(), [name](const auto& capability) {
        return capability.name == name;
    });
    return found == diagnostics.capabilities.end() ? std::string{} : found->detail;
}

} // namespace

RuntimeSnapshot RuntimeSnapshotAssembler::assemble(
    const PlaybackRuntime& playback,
    const QueueRuntime& queue,
    const EqRuntime& eq,
    const RemoteSessionRuntime& remote,
    const SettingsRuntime& settings,
    const application::AppServiceRegistry& services,
    const CreatorAnalysisRuntime& creator,
    std::uint64_t version) const
{
    RuntimeSnapshot result{};
    result.version = version;
    result.playback = playback.snapshot(version);
    result.queue = queue.snapshot(version);
    result.eq = eq.snapshot(version);
    result.remote = remote.snapshot(version);
    result.settings = settings.snapshot(version);

    const auto& session = services.playbackStatus().session();
    result.visualization.available = session.visualization_frame.available;
    result.visualization.bands = session.visualization_frame.bands;
    result.visualization.peaks = session.visualization_frame.bands;
    result.visualization.frame_index = session.transition_generation;
    result.visualization.version = version;

    const auto& lyrics = session.current_lyrics;
    const bool has_synced = lyrics.synced && !lyrics.synced->empty();
    const bool has_plain = lyrics.plain && !lyrics.plain->empty();
    result.lyrics.available = has_synced || has_plain;
    result.lyrics.synced = has_synced;
    result.lyrics.source = lyrics.source;
    result.lyrics.provider = lyrics.source;
    result.lyrics.match_confidence = result.lyrics.available ? "accepted" : "none";
    result.lyrics.version = version;
    if (result.lyrics.available) {
        auto all_lines = lyricLinesFromText(has_synced ? std::string_view{*lyrics.synced} : std::string_view{*lyrics.plain});
        const int active = activeLyricIndex(all_lines, result.playback.elapsed_seconds, result.playback.duration_seconds);
        result.lyrics.current_index = active;
        const int first = std::max(0, active - 3);
        const int last = std::min(static_cast<int>(all_lines.size()), first + 7);
        for (int index = first; index < last; ++index) {
            auto line = all_lines[static_cast<std::size_t>(index)];
            line.current = index == active;
            result.lyrics.visible_lines.push_back(std::move(line));
        }
        result.lyrics.status_message = result.lyrics.synced ? "Synced lyrics available" : "Plain lyrics available";
    } else {
        result.lyrics.current_index = -1;
        result.lyrics.status_message = session.lyrics_lookup_pending ? "Lyrics lookup in progress" : "Lyrics unavailable";
    }

    const auto library_queries = services.libraryQueries();
    const auto& library = library_queries.model();
    const auto library_state = library_queries.state();
    const auto library_scan = library_queries.scanProgress();
    result.library.ready = library_state == app::LibraryIndexState::Ready || library_state == app::LibraryIndexState::Degraded;
    result.library.degraded = library_state == app::LibraryIndexState::Degraded || library.degraded;
    result.library.track_count = static_cast<int>(library.tracks.size());
    result.library.album_count = static_cast<int>(library.albums.size());
    result.library.artist_count = static_cast<int>(library.artists.size());
    result.library.genre_count = static_cast<int>(library.genres.size());
    result.library.status = libraryStateLabel(library_state);
    result.library.scan_phase = std::string{app::libraryScanPhaseLabel(library_scan.phase)};
    result.library.scan_message = library_scan.message;
    result.library.scan_current_path = library_scan.current_path;
    result.library.scan_roots_total = library_scan.roots_total;
    result.library.scan_roots_scanned = library_scan.roots_scanned;
    result.library.scan_files_discovered = library_scan.files_discovered;
    result.library.scan_files_total = library_scan.files_total;
    result.library.scan_files_processed = library_scan.files_processed;
    result.library.scan_tracks_indexed = library_scan.tracks_indexed;
    result.library.version = version;

    result.sources.active_profile_id = result.remote.profile_id;
    result.sources.active_source_label = result.remote.source_label;
    result.sources.connection_status = result.remote.connection_status;
    result.sources.stream_resolved = result.remote.stream_resolved;
    result.sources.configured_count = result.remote.profile_id.empty() ? 0 : 1;
    result.sources.version = version;

    const auto diagnostics = services.diagnostics().snapshot();
    result.diagnostics.runtime_ok = true;
    result.diagnostics.audio_ok = capabilityAvailable(diagnostics, "audio-backend");
    result.diagnostics.library_ok = !result.library.degraded;
    result.diagnostics.remote_ok = capabilityAvailable(diagnostics, "remote-source")
        && capabilityAvailable(diagnostics, "remote-catalog")
        && capabilityAvailable(diagnostics, "remote-stream");
    result.diagnostics.cache_ok = capabilityAvailable(diagnostics, "cache");
    result.diagnostics.audio_backend = capabilityDetail(diagnostics, "audio-backend");
    result.diagnostics.output_device = result.diagnostics.audio_backend;
    result.diagnostics.library_track_count = result.library.track_count;
    result.diagnostics.version = version;
    for (const auto& capability : diagnostics.capabilities) {
        if (!capability.available) {
            result.diagnostics.warnings.push_back(capability.name + " unavailable: " + capability.detail);
        }
    }

    result.plugins.loaded_plugin_ids = services.runtimeServices().plugins.loaded_plugin_ids;
    result.plugins.loaded_count = static_cast<int>(result.plugins.loaded_plugin_ids.size());
    result.plugins.selected_skin_id = services.runtimeServices().plugins.selected_skin_id;
    result.plugins.warnings = services.runtimeServices().plugins.warnings;
    result.plugins.version = version;
    for (const auto& warning : result.plugins.warnings) {
        result.diagnostics.warnings.push_back("plugin: " + warning);
    }

    result.creator = creator.snapshot(version);
    return result;
}

RuntimeSnapshot RuntimeSnapshotAssembler::assemble(
    const PlaybackRuntime& playback,
    const QueueRuntime& queue,
    const EqRuntime& eq,
    const RemoteSessionRuntime& remote,
    const SettingsRuntime& settings,
    std::uint64_t version) const
{
    RuntimeSnapshot result{};
    result.version = version;
    result.playback = playback.snapshot(version);
    result.queue = queue.snapshot(version);
    result.eq = eq.snapshot(version);
    result.remote = remote.snapshot(version);
    result.settings = settings.snapshot(version);
    result.visualization.version = version;
    result.lyrics.version = version;
    result.library.version = version;
    result.sources.version = version;
    result.diagnostics.version = version;
    result.creator.version = version;
    result.plugins.version = version;
    return result;
}

} // namespace lofibox::runtime
