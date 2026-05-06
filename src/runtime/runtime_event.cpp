// SPDX-License-Identifier: GPL-3.0-or-later

#include "runtime/runtime_event.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <string_view>

namespace lofibox::runtime {
namespace {

template <typename Enum>
struct EnumName {
    Enum value;
    std::string_view name;
};

constexpr std::array<EnumName<RuntimeEventKind>, 18> kEventNames{{
    {RuntimeEventKind::RuntimeConnected, "runtime.connected"},
    {RuntimeEventKind::RuntimeDisconnected, "runtime.disconnected"},
    {RuntimeEventKind::RuntimeSnapshot, "runtime.snapshot"},
    {RuntimeEventKind::PlaybackChanged, "playback.changed"},
    {RuntimeEventKind::PlaybackProgress, "playback.progress"},
    {RuntimeEventKind::PlaybackError, "playback.error"},
    {RuntimeEventKind::QueueChanged, "queue.changed"},
    {RuntimeEventKind::EqChanged, "eq.changed"},
    {RuntimeEventKind::RemoteChanged, "remote.changed"},
    {RuntimeEventKind::SettingsChanged, "settings.changed"},
    {RuntimeEventKind::LyricsChanged, "lyrics.changed"},
    {RuntimeEventKind::LyricsLineChanged, "lyrics.line_changed"},
    {RuntimeEventKind::VisualizationFrame, "visualization.frame"},
    {RuntimeEventKind::LibraryScanStarted, "library.scan_started"},
    {RuntimeEventKind::LibraryScanProgress, "library.scan_progress"},
    {RuntimeEventKind::LibraryScanCompleted, "library.scan_completed"},
    {RuntimeEventKind::DiagnosticsChanged, "diagnostics.changed"},
    {RuntimeEventKind::CreatorChanged, "creator.changed"},
}};

template <typename Enum, std::size_t Size>
std::string_view enumName(Enum value, const std::array<EnumName<Enum>, Size>& names) noexcept
{
    for (const auto& entry : names) {
        if (entry.value == value) {
            return entry.name;
        }
    }
    return "runtime.snapshot";
}

template <typename Enum, std::size_t Size>
std::optional<Enum> enumFromName(std::string_view name, const std::array<EnumName<Enum>, Size>& names) noexcept
{
    for (const auto& entry : names) {
        if (entry.name == name) {
            return entry.value;
        }
    }
    return std::nullopt;
}

RuntimeEvent makeEvent(RuntimeEventKind kind, const RuntimeSnapshot& snapshot, std::uint64_t timestamp_ms)
{
    RuntimeEvent event{};
    event.kind = kind;
    event.version = snapshot.version;
    event.timestamp_ms = timestamp_ms;
    event.snapshot = snapshot;
    event.elapsed_seconds = snapshot.playback.elapsed_seconds;
    event.duration_seconds = snapshot.playback.duration_seconds;
    event.current_index = snapshot.lyrics.current_index;
    event.offset_seconds = snapshot.lyrics.offset_seconds;
    event.code = snapshot.playback.error_code;
    event.message = snapshot.playback.error_message;
    if (snapshot.lyrics.current_index >= 0) {
        const auto found = std::find_if(snapshot.lyrics.visible_lines.begin(), snapshot.lyrics.visible_lines.end(), [&snapshot](const RuntimeLyricLine& line) {
            return line.index == snapshot.lyrics.current_index;
        });
        if (found != snapshot.lyrics.visible_lines.end()) {
            event.text = found->text;
        }
    }
    return event;
}

bool playbackIdentityChanged(const PlaybackRuntimeSnapshot& before, const PlaybackRuntimeSnapshot& after)
{
    return before.status != after.status
        || before.current_track_id != after.current_track_id
        || before.title != after.title
        || before.artist != after.artist
        || before.album != after.album
        || before.source_label != after.source_label
        || before.audio_active != after.audio_active
        || before.shuffle_enabled != after.shuffle_enabled
        || before.repeat_all != after.repeat_all
        || before.repeat_one != after.repeat_one;
}

bool playbackProgressChanged(const PlaybackRuntimeSnapshot& before, const PlaybackRuntimeSnapshot& after)
{
    return std::fabs(before.elapsed_seconds - after.elapsed_seconds) >= 0.05
        || before.duration_seconds != after.duration_seconds
        || before.live != after.live
        || before.seekable != after.seekable;
}

bool queueChanged(const QueueRuntimeSnapshot& before, const QueueRuntimeSnapshot& after)
{
    if (before.active_index != after.active_index
        || before.selected_index != after.selected_index
        || before.shuffle_enabled != after.shuffle_enabled
        || before.repeat_all != after.repeat_all
        || before.repeat_one != after.repeat_one
        || before.active_ids != after.active_ids
        || before.visible_items.size() != after.visible_items.size()) {
        return true;
    }
    for (std::size_t index = 0; index < before.visible_items.size(); ++index) {
        const auto& left = before.visible_items[index];
        const auto& right = after.visible_items[index];
        if (left.track_id != right.track_id
            || left.queue_index != right.queue_index
            || left.title != right.title
            || left.artist != right.artist
            || left.album != right.album
            || left.source_label != right.source_label
            || left.active != right.active) {
            return true;
        }
    }
    return false;
}

bool eqChanged(const EqRuntimeSnapshot& before, const EqRuntimeSnapshot& after)
{
    return before.enabled != after.enabled || before.preset_name != after.preset_name || before.bands != after.bands;
}

bool remoteChanged(const RemoteSessionSnapshot& before, const RemoteSessionSnapshot& after)
{
    return before.profile_id != after.profile_id
        || before.source_label != after.source_label
        || before.connection_status != after.connection_status
        || before.stream_resolved != after.stream_resolved
        || before.buffer_state != after.buffer_state
        || before.recovery_action != after.recovery_action
        || before.bitrate_kbps != after.bitrate_kbps
        || before.codec != after.codec
        || before.live != after.live
        || before.seekable != after.seekable;
}

bool settingsChanged(const SettingsRuntimeSnapshot& before, const SettingsRuntimeSnapshot& after)
{
    return before.output_mode != after.output_mode
        || before.network_policy != after.network_policy
        || before.sleep_timer != after.sleep_timer;
}

bool lyricsChanged(const LyricsRuntimeSnapshot& before, const LyricsRuntimeSnapshot& after)
{
    return before.available != after.available
        || before.synced != after.synced
        || before.source != after.source
        || before.provider != after.provider
        || before.match_confidence != after.match_confidence
        || before.status_message != after.status_message
        || before.visible_lines.size() != after.visible_lines.size();
}

bool visualizationChanged(const VisualizationRuntimeSnapshot& before, const VisualizationRuntimeSnapshot& after)
{
    return before.available != after.available
        || before.frame_index != after.frame_index
        || before.bands != after.bands
        || before.peaks != after.peaks
        || std::fabs(before.rms_left - after.rms_left) >= 0.001F
        || std::fabs(before.rms_right - after.rms_right) >= 0.001F
        || std::fabs(before.peak_left - after.peak_left) >= 0.001F
        || std::fabs(before.peak_right - after.peak_right) >= 0.001F;
}

bool libraryChanged(const LibraryRuntimeSnapshot& before, const LibraryRuntimeSnapshot& after)
{
    return before.ready != after.ready
        || before.degraded != after.degraded
        || before.track_count != after.track_count
        || before.album_count != after.album_count
        || before.artist_count != after.artist_count
        || before.genre_count != after.genre_count
        || before.status != after.status
        || before.scan_phase != after.scan_phase
        || before.scan_message != after.scan_message
        || before.scan_current_path != after.scan_current_path
        || before.scan_roots_total != after.scan_roots_total
        || before.scan_roots_scanned != after.scan_roots_scanned
        || before.scan_files_discovered != after.scan_files_discovered
        || before.scan_files_total != after.scan_files_total
        || before.scan_files_processed != after.scan_files_processed
        || before.scan_tracks_indexed != after.scan_tracks_indexed;
}

bool diagnosticsChanged(const DiagnosticsRuntimeSnapshot& before, const DiagnosticsRuntimeSnapshot& after)
{
    return before.runtime_ok != after.runtime_ok
        || before.audio_ok != after.audio_ok
        || before.library_ok != after.library_ok
        || before.remote_ok != after.remote_ok
        || before.cache_ok != after.cache_ok
        || before.warnings != after.warnings
        || before.errors != after.errors;
}

bool creatorChanged(const CreatorRuntimeSnapshot& before, const CreatorRuntimeSnapshot& after)
{
    return before.available != after.available
        || std::fabs(before.bpm - after.bpm) >= 0.1
        || before.key != after.key
        || std::fabs(before.lufs - after.lufs) >= 0.1
        || std::fabs(before.dynamic_range - after.dynamic_range) >= 0.1
        || before.waveform_points != after.waveform_points
        || before.beat_grid_seconds != after.beat_grid_seconds
        || before.loop_marker_seconds != after.loop_marker_seconds
        || before.section_markers != after.section_markers
        || before.stem_status != after.stem_status
        || before.status_message != after.status_message;
}

RuntimeEventKind libraryEventKind(const LibraryRuntimeSnapshot& before, const LibraryRuntimeSnapshot& after)
{
    if (before.status != "LOADING" && after.status == "LOADING") {
        return RuntimeEventKind::LibraryScanStarted;
    }
    if (before.status == "LOADING" && (after.status == "READY" || after.status == "DEGRADED")) {
        return RuntimeEventKind::LibraryScanCompleted;
    }
    return RuntimeEventKind::LibraryScanProgress;
}

} // namespace

std::string runtimeEventKindName(RuntimeEventKind kind) noexcept
{
    return std::string{enumName(kind, kEventNames)};
}

std::optional<RuntimeEventKind> runtimeEventKindFromName(std::string_view name) noexcept
{
    return enumFromName(name, kEventNames);
}

std::uint64_t runtimeEventTimestampMs() noexcept
{
    const auto now = std::chrono::system_clock::now().time_since_epoch();
    return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(now).count());
}

std::vector<RuntimeEvent> runtimeEventsBetween(
    const std::optional<RuntimeSnapshot>& previous,
    const RuntimeSnapshot& next,
    std::uint64_t timestamp_ms)
{
    std::vector<RuntimeEvent> events{};
    if (!previous) {
        events.push_back(makeEvent(RuntimeEventKind::RuntimeConnected, next, timestamp_ms));
        events.push_back(makeEvent(RuntimeEventKind::RuntimeSnapshot, next, timestamp_ms));
        if (next.visualization.available) {
            events.push_back(makeEvent(RuntimeEventKind::VisualizationFrame, next, timestamp_ms));
        }
        if (next.lyrics.available) {
            events.push_back(makeEvent(RuntimeEventKind::LyricsChanged, next, timestamp_ms));
            if (next.lyrics.current_index >= 0) {
                events.push_back(makeEvent(RuntimeEventKind::LyricsLineChanged, next, timestamp_ms));
            }
        }
        if (next.creator.available) {
            events.push_back(makeEvent(RuntimeEventKind::CreatorChanged, next, timestamp_ms));
        }
        return events;
    }

    const auto& before = *previous;
    if (playbackIdentityChanged(before.playback, next.playback)) {
        events.push_back(makeEvent(RuntimeEventKind::PlaybackChanged, next, timestamp_ms));
    }
    if (playbackProgressChanged(before.playback, next.playback)) {
        events.push_back(makeEvent(RuntimeEventKind::PlaybackProgress, next, timestamp_ms));
    }
    if (before.playback.error_code != next.playback.error_code || before.playback.error_message != next.playback.error_message) {
        events.push_back(makeEvent(RuntimeEventKind::PlaybackError, next, timestamp_ms));
    }
    if (queueChanged(before.queue, next.queue)) {
        events.push_back(makeEvent(RuntimeEventKind::QueueChanged, next, timestamp_ms));
    }
    if (eqChanged(before.eq, next.eq)) {
        events.push_back(makeEvent(RuntimeEventKind::EqChanged, next, timestamp_ms));
    }
    if (remoteChanged(before.remote, next.remote)) {
        events.push_back(makeEvent(RuntimeEventKind::RemoteChanged, next, timestamp_ms));
    }
    if (settingsChanged(before.settings, next.settings)) {
        events.push_back(makeEvent(RuntimeEventKind::SettingsChanged, next, timestamp_ms));
    }
    if (lyricsChanged(before.lyrics, next.lyrics)) {
        events.push_back(makeEvent(RuntimeEventKind::LyricsChanged, next, timestamp_ms));
    }
    if (before.lyrics.current_index != next.lyrics.current_index) {
        events.push_back(makeEvent(RuntimeEventKind::LyricsLineChanged, next, timestamp_ms));
    }
    if (visualizationChanged(before.visualization, next.visualization)) {
        events.push_back(makeEvent(RuntimeEventKind::VisualizationFrame, next, timestamp_ms));
    }
    if (libraryChanged(before.library, next.library)) {
        events.push_back(makeEvent(libraryEventKind(before.library, next.library), next, timestamp_ms));
    }
    if (diagnosticsChanged(before.diagnostics, next.diagnostics)) {
        events.push_back(makeEvent(RuntimeEventKind::DiagnosticsChanged, next, timestamp_ms));
    }
    if (creatorChanged(before.creator, next.creator)) {
        events.push_back(makeEvent(RuntimeEventKind::CreatorChanged, next, timestamp_ms));
    }
    return events;
}

} // namespace lofibox::runtime
