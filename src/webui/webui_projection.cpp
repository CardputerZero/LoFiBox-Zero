// SPDX-License-Identifier: GPL-3.0-or-later

#include "webui/webui_projection.h"

#include <sstream>

#include "webui/webui_json.h"

namespace lofibox::webui {
namespace {

using json::separator;
using json::appendString;
using json::appendBool;
using json::appendInt;
using json::appendDouble;
using json::openObject;
using json::closeObject;
using json::openArray;
using json::closeArray;
using json::appendStringArray;
using json::appendIntArray;
using json::appendInt64;

std::string statusString(runtime::RuntimePlaybackStatus status)
{
    switch (status) {
    case runtime::RuntimePlaybackStatus::Playing: return "playing";
    case runtime::RuntimePlaybackStatus::Paused:  return "paused";
    case runtime::RuntimePlaybackStatus::Empty:   return "empty";
    }
    return "empty";
}

void appendPlaybackFields(std::ostringstream& out, const runtime::PlaybackRuntimeSnapshot& p)
{
    appendString(out, "status", statusString(p.status));
    separator(out); appendBool(out, "audio_active", p.audio_active);
    separator(out); appendInt(out, "current_track_id", p.current_track_id.value_or(-1));
    separator(out); appendString(out, "title", p.title);
    separator(out); appendString(out, "artist", p.artist);
    separator(out); appendString(out, "album", p.album);
    separator(out); appendString(out, "album_artist", p.album_artist);
    separator(out); appendString(out, "source_label", p.source_label);
    separator(out); appendString(out, "source_type", p.source_type);
    separator(out); appendDouble(out, "elapsed_seconds", p.elapsed_seconds);
    separator(out); appendInt(out, "elapsed_ms", static_cast<int>(p.elapsed_seconds * 1000.0));
    separator(out); appendInt(out, "duration_seconds", p.duration_seconds);
    separator(out); appendInt(out, "duration_ms", p.duration_seconds * 1000);
    separator(out); appendBool(out, "seekable", p.seekable);
    separator(out); appendBool(out, "live", p.live);
    separator(out); appendInt(out, "volume_percent", p.volume_percent);
    separator(out); appendBool(out, "muted", p.muted);
    separator(out); appendString(out, "codec", p.codec);
    separator(out); appendInt(out, "bitrate_kbps", p.bitrate_kbps);
    separator(out); appendInt(out, "sample_rate_hz", p.sample_rate_hz);
    separator(out); appendInt(out, "bit_depth", p.bit_depth);
    separator(out); appendBool(out, "shuffle_enabled", p.shuffle_enabled);
    separator(out); appendBool(out, "repeat_all", p.repeat_all);
    separator(out); appendBool(out, "repeat_one", p.repeat_one);
    separator(out); appendString(out, "error_code", p.error_code);
    separator(out); appendString(out, "error_message", p.error_message);
}

void appendQueueFields(std::ostringstream& out, const runtime::QueueRuntimeSnapshot& q)
{
    appendInt(out, "count", static_cast<int>(q.visible_items.size()));
    separator(out); appendInt(out, "active_index", q.active_index);
    separator(out); appendInt(out, "selected_index", q.selected_index);
    separator(out); appendBool(out, "shuffle_enabled", q.shuffle_enabled);
    separator(out); appendBool(out, "repeat_all", q.repeat_all);
    separator(out); appendBool(out, "repeat_one", q.repeat_one);

    separator(out); openArray(out, "items");
    for (std::size_t index = 0; index < q.visible_items.size(); ++index) {
        if (index > 0) out << ',';
        const auto& item = q.visible_items[index];
        out << '{';
        appendInt(out, "track_id", item.track_id);
        separator(out); appendInt(out, "queue_index", item.queue_index);
        separator(out); appendString(out, "title", item.title);
        separator(out); appendString(out, "artist", item.artist);
        separator(out); appendString(out, "album", item.album);
        separator(out); appendString(out, "source_label", item.source_label);
        separator(out); appendInt(out, "duration_seconds", item.duration_seconds);
        separator(out); appendBool(out, "active", item.active);
        separator(out); appendBool(out, "playable", item.playable);
        out << '}';
    }
    closeArray(out);
}

void appendEqFields(std::ostringstream& out, const runtime::EqRuntimeSnapshot& eq)
{
    appendBool(out, "enabled", eq.enabled);
    separator(out); appendString(out, "preset_name", eq.preset_name);

    separator(out); openArray(out, "bands");
    for (std::size_t index = 0; index < eq.bands.size(); ++index) {
        if (index > 0) out << ',';
        out << eq.bands[index];
    }
    closeArray(out);
}

void appendLyricsFields(std::ostringstream& out, const runtime::LyricsRuntimeSnapshot& l)
{
    appendBool(out, "available", l.available);
    separator(out); appendBool(out, "synced", l.synced);
    separator(out); appendString(out, "source", l.source);
    separator(out); appendString(out, "provider", l.provider);
    separator(out); appendInt(out, "current_index", l.current_index);
    separator(out); appendDouble(out, "offset_seconds", l.offset_seconds);
    separator(out); appendString(out, "match_confidence", l.match_confidence);
    separator(out); appendString(out, "status_message", l.status_message);

    separator(out); openArray(out, "lines");
    for (std::size_t index = 0; index < l.visible_lines.size(); ++index) {
        if (index > 0) out << ',';
        const auto& line = l.visible_lines[index];
        out << '{';
        appendInt(out, "index", line.index);
        separator(out); appendDouble(out, "timestamp_seconds", line.timestamp_seconds);
        separator(out); appendString(out, "text", line.text);
        separator(out); appendString(out, "translation", line.translation);
        separator(out); appendBool(out, "current", line.current);
        out << '}';
    }
    closeArray(out);
}

void appendVisualizationFields(std::ostringstream& out, const runtime::VisualizationRuntimeSnapshot& v)
{
    appendBool(out, "available", v.available);
    separator(out); appendString(out, "mode", v.mode);

    separator(out); openArray(out, "bands");
    for (std::size_t index = 0; index < v.bands.size(); ++index) {
        if (index > 0) out << ',';
        out << v.bands[index];
    }
    closeArray(out);

    separator(out); openArray(out, "peaks");
    for (std::size_t index = 0; index < v.peaks.size(); ++index) {
        if (index > 0) out << ',';
        out << v.peaks[index];
    }
    closeArray(out);

    separator(out); appendDouble(out, "rms_left", static_cast<double>(v.rms_left));
    separator(out); appendDouble(out, "rms_right", static_cast<double>(v.rms_right));
    separator(out); appendDouble(out, "peak_left", static_cast<double>(v.peak_left));
    separator(out); appendDouble(out, "peak_right", static_cast<double>(v.peak_right));
}

void appendRemoteFields(std::ostringstream& out, const runtime::RemoteSessionSnapshot& r)
{
    appendString(out, "profile_id", r.profile_id);
    separator(out); appendString(out, "source_label", r.source_label);
    separator(out); appendString(out, "connection_status", r.connection_status);
    separator(out); appendBool(out, "stream_resolved", r.stream_resolved);
    separator(out); appendString(out, "buffer_state", r.buffer_state);
    separator(out); appendInt(out, "bitrate_kbps", r.bitrate_kbps);
    separator(out); appendString(out, "codec", r.codec);
    separator(out); appendBool(out, "live", r.live);
    separator(out); appendBool(out, "seekable", r.seekable);
}

void appendLibraryFields(std::ostringstream& out, const runtime::LibraryRuntimeSnapshot& lib)
{
    appendBool(out, "ready", lib.ready);
    separator(out); appendBool(out, "degraded", lib.degraded);
    separator(out); appendInt(out, "track_count", lib.track_count);
    separator(out); appendInt(out, "album_count", lib.album_count);
    separator(out); appendInt(out, "artist_count", lib.artist_count);
    separator(out); appendInt(out, "genre_count", lib.genre_count);
    separator(out); appendString(out, "status", lib.status);
}

void appendSourcesFields(std::ostringstream& out, const runtime::SourceRuntimeSnapshot& src)
{
    appendInt(out, "configured_count", src.configured_count);
    separator(out); appendString(out, "active_profile_id", src.active_profile_id);
    separator(out); appendString(out, "active_source_label", src.active_source_label);
    separator(out); appendString(out, "connection_status", src.connection_status);
    separator(out); appendBool(out, "stream_resolved", src.stream_resolved);
}

void appendSettingsFields(std::ostringstream& out, const runtime::SettingsRuntimeSnapshot& s)
{
    appendString(out, "output_mode", s.output_mode);
    separator(out); appendString(out, "network_policy", s.network_policy);
    separator(out); appendString(out, "sleep_timer", s.sleep_timer);
}

void appendDiagnosticsFields(std::ostringstream& out, const runtime::DiagnosticsRuntimeSnapshot& d)
{
    appendBool(out, "runtime_ok", d.runtime_ok);
    separator(out); appendBool(out, "audio_ok", d.audio_ok);
    separator(out); appendBool(out, "library_ok", d.library_ok);
    separator(out); appendBool(out, "remote_ok", d.remote_ok);
    separator(out); appendBool(out, "cache_ok", d.cache_ok);
    separator(out); appendString(out, "audio_backend", d.audio_backend);
    separator(out); appendString(out, "output_device", d.output_device);
    separator(out); appendInt(out, "library_track_count", d.library_track_count);
    separator(out); appendInt(out, "failed_scan_count", d.failed_scan_count);
    separator(out); appendStringArray(out, "warnings", d.warnings);
    separator(out); appendStringArray(out, "errors", d.errors);
}

void appendPluginFields(std::ostringstream& out, const runtime::PluginRuntimeSnapshot& p)
{
    appendInt(out, "loaded_count", p.loaded_count);
    separator(out); appendString(out, "selected_skin_id", p.selected_skin_id);
    separator(out); appendStringArray(out, "loaded_plugin_ids", p.loaded_plugin_ids);
    separator(out); appendStringArray(out, "warnings", p.warnings);
}

} // namespace

std::string buildNowPlayingJson(const runtime::RuntimeSnapshot& snapshot)
{
    std::ostringstream out;
    out << '{';

    openObject(out, "playback");
    appendPlaybackFields(out, snapshot.playback);
    closeObject(out);

    separator(out); openObject(out, "queue_summary");
    appendInt(out, "count", static_cast<int>(snapshot.queue.visible_items.size()));
    separator(out); appendInt(out, "active_index", snapshot.queue.active_index);
    separator(out); appendBool(out, "repeat_all", snapshot.queue.repeat_all);
    separator(out); appendBool(out, "repeat_one", snapshot.queue.repeat_one);
    separator(out); appendBool(out, "shuffle_enabled", snapshot.queue.shuffle_enabled);
    closeObject(out);

    separator(out); openObject(out, "lyrics");
    appendLyricsFields(out, snapshot.lyrics);
    closeObject(out);

    separator(out); openObject(out, "visualization");
    appendVisualizationFields(out, snapshot.visualization);
    closeObject(out);

    separator(out); openObject(out, "remote");
    appendRemoteFields(out, snapshot.remote);
    closeObject(out);

    separator(out); appendInt64(out, "version", static_cast<std::int64_t>(snapshot.version));
    out << '}';
    return out.str();
}

std::string buildQueueJson(const runtime::RuntimeSnapshot& snapshot)
{
    std::ostringstream out;
    out << '{';

    openObject(out, "queue");
    appendQueueFields(out, snapshot.queue);
    closeObject(out);

    separator(out); openObject(out, "playback");
    appendString(out, "status", statusString(snapshot.playback.status));
    separator(out); appendString(out, "title", snapshot.playback.title);
    separator(out); appendString(out, "artist", snapshot.playback.artist);
    closeObject(out);

    separator(out); appendInt64(out, "version", static_cast<std::int64_t>(snapshot.version));
    out << '}';
    return out.str();
}

std::string buildLibraryJson(const runtime::RuntimeSnapshot& snapshot)
{
    std::ostringstream out;
    out << '{';

    openObject(out, "library");
    appendLibraryFields(out, snapshot.library);
    closeObject(out);

    separator(out); appendInt64(out, "version", static_cast<std::int64_t>(snapshot.version));
    out << '}';
    return out.str();
}

std::string buildSourcesJson(const runtime::RuntimeSnapshot& snapshot)
{
    std::ostringstream out;
    out << '{';

    openObject(out, "sources");
    appendSourcesFields(out, snapshot.sources);
    closeObject(out);

    separator(out); openObject(out, "remote");
    appendRemoteFields(out, snapshot.remote);
    closeObject(out);

    separator(out); appendInt64(out, "version", static_cast<std::int64_t>(snapshot.version));
    out << '}';
    return out.str();
}

std::string buildEqJson(const runtime::RuntimeSnapshot& snapshot)
{
    std::ostringstream out;
    out << '{';

    openObject(out, "eq");
    appendEqFields(out, snapshot.eq);
    closeObject(out);

    separator(out); appendInt64(out, "version", static_cast<std::int64_t>(snapshot.version));
    out << '}';
    return out.str();
}

std::string buildSettingsJson(const runtime::RuntimeSnapshot& snapshot)
{
    std::ostringstream out;
    out << '{';

    openObject(out, "settings");
    appendSettingsFields(out, snapshot.settings);
    closeObject(out);

    separator(out); appendInt64(out, "version", static_cast<std::int64_t>(snapshot.version));
    out << '}';
    return out.str();
}

std::string buildDiagnosticsJson(const runtime::RuntimeSnapshot& snapshot)
{
    std::ostringstream out;
    out << '{';

    openObject(out, "diagnostics");
    appendDiagnosticsFields(out, snapshot.diagnostics);
    closeObject(out);

    separator(out); openObject(out, "plugins");
    appendPluginFields(out, snapshot.plugins);
    closeObject(out);

    separator(out); appendInt64(out, "version", static_cast<std::int64_t>(snapshot.version));
    out << '}';
    return out.str();
}

std::string buildFullSnapshotJson(const runtime::RuntimeSnapshot& snapshot)
{
    std::ostringstream out;
    out << '{';

    openObject(out, "playback");
    appendPlaybackFields(out, snapshot.playback);
    closeObject(out);

    separator(out); openObject(out, "queue");
    appendQueueFields(out, snapshot.queue);
    closeObject(out);

    separator(out); openObject(out, "eq");
    appendEqFields(out, snapshot.eq);
    closeObject(out);

    separator(out); openObject(out, "lyrics");
    appendLyricsFields(out, snapshot.lyrics);
    closeObject(out);

    separator(out); openObject(out, "visualization");
    appendVisualizationFields(out, snapshot.visualization);
    closeObject(out);

    separator(out); openObject(out, "remote");
    appendRemoteFields(out, snapshot.remote);
    closeObject(out);

    separator(out); openObject(out, "library");
    appendLibraryFields(out, snapshot.library);
    closeObject(out);

    separator(out); openObject(out, "sources");
    appendSourcesFields(out, snapshot.sources);
    closeObject(out);

    separator(out); openObject(out, "settings");
    appendSettingsFields(out, snapshot.settings);
    closeObject(out);

    separator(out); openObject(out, "diagnostics");
    appendDiagnosticsFields(out, snapshot.diagnostics);
    closeObject(out);

    separator(out); openObject(out, "plugins");
    appendPluginFields(out, snapshot.plugins);
    closeObject(out);

    separator(out); appendInt64(out, "version", static_cast<std::int64_t>(snapshot.version));
    out << '}';
    return out.str();
}

std::string buildEventJson(const runtime::RuntimeEvent& event)
{
    std::ostringstream out;
    out << '{';

    appendString(out, "event", runtime::runtimeEventKindName(event.kind));

    separator(out); openObject(out, "snapshot");
    appendPlaybackFields(out, event.snapshot.playback);
    closeObject(out);

    // Include visualization bands for real-time spectrum
    if (event.snapshot.visualization.available) {
        separator(out); openObject(out, "visualization");
        appendBool(out, "available", true);
        separator(out); openArray(out, "bands");
        for (std::size_t i = 0; i < event.snapshot.visualization.bands.size(); ++i) {
            if (i > 0) out << ',';
            out << event.snapshot.visualization.bands[i];
        }
        closeArray(out);
        closeObject(out);
    }

    // Include lyrics lines for real-time sync
    if (event.snapshot.lyrics.available) {
        separator(out); openObject(out, "lyrics");
        appendBool(out, "available", true);
        separator(out); appendInt(out, "current_index", event.snapshot.lyrics.current_index);
        separator(out); openArray(out, "lines");
        for (std::size_t i = 0; i < event.snapshot.lyrics.visible_lines.size(); ++i) {
            if (i > 0) out << ',';
            const auto& line = event.snapshot.lyrics.visible_lines[i];
            out << '{';
            appendInt(out, "index", line.index);
            separator(out); appendDouble(out, "timestamp_seconds", line.timestamp_seconds);
            separator(out); appendString(out, "text", line.text);
            separator(out); appendBool(out, "current", line.current);
            out << '}';
        }
        closeArray(out);
        closeObject(out);
    }

    separator(out); appendInt64(out, "stream_version", static_cast<std::int64_t>(event.version));
    separator(out); appendDouble(out, "elapsed_seconds", event.elapsed_seconds);
    separator(out); appendInt(out, "duration_seconds", event.duration_seconds);
    separator(out); appendInt(out, "current_index", event.current_index);
    separator(out); appendString(out, "text", event.text);
    separator(out); appendString(out, "code", event.code);
    separator(out); appendString(out, "message", event.message);
    separator(out); appendInt64(out, "timestamp_ms", static_cast<std::int64_t>(event.timestamp_ms));
    out << '}';
    return out.str();
}

} // namespace lofibox::webui
