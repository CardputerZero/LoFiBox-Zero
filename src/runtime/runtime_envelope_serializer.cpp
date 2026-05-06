// SPDX-License-Identifier: GPL-3.0-or-later

#include "runtime/runtime_envelope_serializer.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cctype>
#include <sstream>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace lofibox::runtime {
namespace {

template <typename Enum>
struct EnumName {
    Enum value;
    std::string_view name;
};

constexpr std::array<EnumName<RuntimeCommandKind>, 29> kCommandNames{{
    {RuntimeCommandKind::PlaybackPlay, "PlaybackPlay"},
    {RuntimeCommandKind::PlaybackPause, "PlaybackPause"},
    {RuntimeCommandKind::PlaybackResume, "PlaybackResume"},
    {RuntimeCommandKind::PlaybackToggle, "PlaybackToggle"},
    {RuntimeCommandKind::PlaybackStartTrack, "PlaybackStartTrack"},
    {RuntimeCommandKind::PlaybackStop, "PlaybackStop"},
    {RuntimeCommandKind::PlaybackSeek, "PlaybackSeek"},
    {RuntimeCommandKind::QueueStep, "QueueStep"},
    {RuntimeCommandKind::QueueJump, "QueueJump"},
    {RuntimeCommandKind::QueueClear, "QueueClear"},
    {RuntimeCommandKind::PlaybackToggleShuffle, "PlaybackToggleShuffle"},
    {RuntimeCommandKind::PlaybackSetShuffle, "PlaybackSetShuffle"},
    {RuntimeCommandKind::PlaybackCycleRepeat, "PlaybackCycleRepeat"},
    {RuntimeCommandKind::PlaybackCycleMainMenuMode, "PlaybackCycleMainMenuMode"},
    {RuntimeCommandKind::PlaybackSetRepeatAll, "PlaybackSetRepeatAll"},
    {RuntimeCommandKind::PlaybackSetRepeatOne, "PlaybackSetRepeatOne"},
    {RuntimeCommandKind::RemoteResolveAndStartTrack, "RemoteResolveAndStartTrack"},
    {RuntimeCommandKind::RemoteStartActiveStream, "RemoteStartActiveStream"},
    {RuntimeCommandKind::EqEnable, "EqEnable"},
    {RuntimeCommandKind::EqDisable, "EqDisable"},
    {RuntimeCommandKind::EqSetBand, "EqSetBand"},
    {RuntimeCommandKind::EqAdjustBand, "EqAdjustBand"},
    {RuntimeCommandKind::EqApplyPreset, "EqApplyPreset"},
    {RuntimeCommandKind::EqCyclePreset, "EqCyclePreset"},
    {RuntimeCommandKind::EqReset, "EqReset"},
    {RuntimeCommandKind::RemoteReconnect, "RemoteReconnect"},
    {RuntimeCommandKind::SettingsApplyLive, "SettingsApplyLive"},
    {RuntimeCommandKind::RuntimeShutdown, "RuntimeShutdown"},
    {RuntimeCommandKind::RuntimeReload, "RuntimeReload"},
}};

constexpr std::array<EnumName<RuntimeQueryKind>, 6> kQueryNames{{
    {RuntimeQueryKind::PlaybackSnapshot, "PlaybackSnapshot"},
    {RuntimeQueryKind::QueueSnapshot, "QueueSnapshot"},
    {RuntimeQueryKind::EqSnapshot, "EqSnapshot"},
    {RuntimeQueryKind::RemoteSessionSnapshot, "RemoteSessionSnapshot"},
    {RuntimeQueryKind::SettingsSnapshot, "SettingsSnapshot"},
    {RuntimeQueryKind::FullSnapshot, "FullSnapshot"},
}};

constexpr std::array<EnumName<CommandOrigin>, 6> kOriginNames{{
    {CommandOrigin::Gui, "Gui"},
    {CommandOrigin::DirectTest, "DirectTest"},
    {CommandOrigin::Desktop, "Desktop"},
    {CommandOrigin::RuntimeCli, "RuntimeCli"},
    {CommandOrigin::Automation, "Automation"},
    {CommandOrigin::Unknown, "Unknown"},
}};

template <typename Enum, std::size_t Size>
std::string_view enumName(Enum value, const std::array<EnumName<Enum>, Size>& names) noexcept
{
    for (const auto& entry : names) {
        if (entry.value == value) {
            return entry.name;
        }
    }
    return "Unknown";
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

std::string escapeJson(std::string_view text)
{
    std::string out;
    out.reserve(text.size() + 2);
    for (const char ch : text) {
        switch (ch) {
        case '\\': out += "\\\\"; break;
        case '"': out += "\\\""; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default: out += ch; break;
        }
    }
    return out;
}

void appendStringField(std::ostringstream& out, std::string_view key, std::string_view value)
{
    out << ",\"" << key << "\":\"" << escapeJson(value) << '"';
}

void appendBoolField(std::ostringstream& out, std::string_view key, bool value)
{
    out << ",\"" << key << "\":" << (value ? "true" : "false");
}

void appendIntArray(std::ostringstream& out, std::string_view key, const std::vector<int>& values)
{
    out << ",\"" << key << "\":[";
    for (std::size_t index = 0; index < values.size(); ++index) {
        if (index > 0) {
            out << ',';
        }
        out << values[index];
    }
    out << ']';
}

template <std::size_t Size>
void appendIntArray(std::ostringstream& out, std::string_view key, const std::array<int, Size>& values)
{
    out << ",\"" << key << "\":[";
    for (std::size_t index = 0; index < values.size(); ++index) {
        if (index > 0) {
            out << ',';
        }
        out << values[index];
    }
    out << ']';
}

template <std::size_t Size>
void appendFloatArray(std::ostringstream& out, std::string_view key, const std::array<float, Size>& values)
{
    out << ",\"" << key << "\":[";
    for (std::size_t index = 0; index < values.size(); ++index) {
        if (index > 0) {
            out << ',';
        }
        out << values[index];
    }
    out << ']';
}

void appendFloatArray(std::ostringstream& out, std::string_view key, const std::vector<float>& values)
{
    out << ",\"" << key << "\":[";
    for (std::size_t index = 0; index < values.size(); ++index) {
        if (index > 0) {
            out << ',';
        }
        out << values[index];
    }
    out << ']';
}

void appendDoubleArray(std::ostringstream& out, std::string_view key, const std::vector<double>& values)
{
    out << ",\"" << key << "\":[";
    for (std::size_t index = 0; index < values.size(); ++index) {
        if (index > 0) {
            out << ',';
        }
        out << values[index];
    }
    out << ']';
}

void appendStringArray(std::ostringstream& out, std::string_view key, const std::vector<std::string>& values)
{
    out << ",\"" << key << "\":[";
    for (std::size_t index = 0; index < values.size(); ++index) {
        if (index > 0) {
            out << ',';
        }
        out << '"' << escapeJson(values[index]) << '"';
    }
    out << ']';
}

std::optional<std::size_t> fieldValueStart(std::string_view json, std::string_view key)
{
    const auto quoted_key = "\"" + std::string{key} + "\":";
    const auto found = json.find(quoted_key);
    if (found == std::string_view::npos) {
        return std::nullopt;
    }
    auto pos = found + quoted_key.size();
    while (pos < json.size() && std::isspace(static_cast<unsigned char>(json[pos]))) {
        ++pos;
    }
    return pos;
}

std::optional<std::string> stringField(std::string_view json, std::string_view key)
{
    const auto start = fieldValueStart(json, key);
    if (!start || *start >= json.size() || json[*start] != '"') {
        return std::nullopt;
    }
    std::string value;
    for (std::size_t pos = *start + 1; pos < json.size(); ++pos) {
        const char ch = json[pos];
        if (ch == '"') {
            return value;
        }
        if (ch == '\\' && (pos + 1) < json.size()) {
            const char escaped = json[++pos];
            switch (escaped) {
            case 'n': value.push_back('\n'); break;
            case 'r': value.push_back('\r'); break;
            case 't': value.push_back('\t'); break;
            default: value.push_back(escaped); break;
            }
            continue;
        }
        value.push_back(ch);
    }
    return std::nullopt;
}

template <typename T>
std::optional<T> numberField(std::string_view json, std::string_view key)
{
    const auto start = fieldValueStart(json, key);
    if (!start) {
        return std::nullopt;
    }
    std::size_t end = *start;
    while (end < json.size() && (std::isdigit(static_cast<unsigned char>(json[end])) || json[end] == '-' || json[end] == '+' || json[end] == '.')) {
        ++end;
    }
    T value{};
    const auto text = json.substr(*start, end - *start);
    const auto* begin = text.data();
    const auto* finish = text.data() + text.size();
    const auto parsed = std::from_chars(begin, finish, value);
    if (parsed.ec != std::errc{}) {
        return std::nullopt;
    }
    return value;
}

std::optional<bool> boolField(std::string_view json, std::string_view key)
{
    const auto start = fieldValueStart(json, key);
    if (!start) {
        return std::nullopt;
    }
    if (json.substr(*start, 4) == "true") {
        return true;
    }
    if (json.substr(*start, 5) == "false") {
        return false;
    }
    return std::nullopt;
}

std::vector<int> intArrayField(std::string_view json, std::string_view key)
{
    std::vector<int> values;
    const auto start = fieldValueStart(json, key);
    if (!start || *start >= json.size() || json[*start] != '[') {
        return values;
    }
    const auto end = json.find(']', *start + 1);
    if (end == std::string_view::npos) {
        return values;
    }
    std::size_t pos = *start + 1;
    while (pos < end) {
        while (pos < end && (json[pos] == ',' || std::isspace(static_cast<unsigned char>(json[pos])))) {
            ++pos;
        }
        std::size_t next = pos;
        while (next < end && json[next] != ',') {
            ++next;
        }
        int value{};
        const auto text = json.substr(pos, next - pos);
        const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
        if (parsed.ec == std::errc{}) {
            values.push_back(value);
        }
        pos = next + 1;
    }
    return values;
}

std::vector<double> numberArrayField(std::string_view json, std::string_view key)
{
    std::vector<double> values;
    const auto start = fieldValueStart(json, key);
    if (!start || *start >= json.size() || json[*start] != '[') {
        return values;
    }
    const auto end = json.find(']', *start + 1);
    if (end == std::string_view::npos) {
        return values;
    }
    std::size_t pos = *start + 1;
    while (pos < end) {
        while (pos < end && (json[pos] == ',' || std::isspace(static_cast<unsigned char>(json[pos])))) {
            ++pos;
        }
        std::size_t next = pos;
        while (next < end && json[next] != ',') {
            ++next;
        }
        double value{};
        const auto text = json.substr(pos, next - pos);
        const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
        if (parsed.ec == std::errc{}) {
            values.push_back(value);
        }
        pos = next + 1;
    }
    return values;
}

std::vector<std::string> stringArrayField(std::string_view json, std::string_view key)
{
    std::vector<std::string> values;
    const auto start = fieldValueStart(json, key);
    if (!start || *start >= json.size() || json[*start] != '[') {
        return values;
    }
    std::size_t pos = *start + 1;
    while (pos < json.size()) {
        while (pos < json.size() && (json[pos] == ',' || std::isspace(static_cast<unsigned char>(json[pos])))) {
            ++pos;
        }
        if (pos >= json.size() || json[pos] == ']') {
            break;
        }
        if (json[pos] != '"') {
            break;
        }
        std::string value;
        ++pos;
        while (pos < json.size()) {
            const char ch = json[pos++];
            if (ch == '"') {
                values.push_back(std::move(value));
                break;
            }
            if (ch == '\\' && pos < json.size()) {
                const char escaped = json[pos++];
                switch (escaped) {
                case 'n': value.push_back('\n'); break;
                case 'r': value.push_back('\r'); break;
                case 't': value.push_back('\t'); break;
                default: value.push_back(escaped); break;
                }
                continue;
            }
            value.push_back(ch);
        }
    }
    return values;
}

void appendPayload(std::ostringstream& out, const RuntimeCommandPayload& payload)
{
    std::visit([&out](const auto& data) {
        using Payload = std::decay_t<decltype(data)>;
        if constexpr (std::is_same_v<Payload, EmptyPayload>) {
            appendStringField(out, "payload", "Empty");
        } else if constexpr (std::is_same_v<Payload, PlaybackStartTrackPayload>) {
            appendStringField(out, "payload", "PlaybackStartTrack");
            out << ",\"track_id\":" << data.track_id;
            if (!data.album.empty()) out << ",\"album\":\"" << escapeJson(data.album) << '"';
            if (!data.artist.empty()) out << ",\"artist\":\"" << escapeJson(data.artist) << '"';
            if (!data.genre.empty()) out << ",\"genre\":\"" << escapeJson(data.genre) << '"';
        } else if constexpr (std::is_same_v<Payload, PlaybackSeekPayload>) {
            appendStringField(out, "payload", "PlaybackSeek");
            out << ",\"seconds\":" << data.seconds;
        } else if constexpr (std::is_same_v<Payload, QueueStepPayload>) {
            appendStringField(out, "payload", "QueueStep");
            out << ",\"delta\":" << data.delta;
        } else if constexpr (std::is_same_v<Payload, QueueIndexPayload>) {
            appendStringField(out, "payload", "QueueIndex");
            out << ",\"queue_index\":" << data.queue_index;
        } else if constexpr (std::is_same_v<Payload, RuntimeEnabledPayload>) {
            appendStringField(out, "payload", "RuntimeEnabled");
            appendBoolField(out, "enabled", data.enabled);
        } else if constexpr (std::is_same_v<Payload, RemoteTrackRefPayload>) {
            appendStringField(out, "payload", "RemoteTrackRef");
            appendStringField(out, "profile_id", data.profile_id);
            appendStringField(out, "item_id", data.item_id);
        } else if constexpr (std::is_same_v<Payload, EqSetBandPayload>) {
            appendStringField(out, "payload", "EqSetBand");
            out << ",\"eq_band_index\":" << data.eq_band_index << ",\"eq_gain_db\":" << data.eq_gain_db;
        } else if constexpr (std::is_same_v<Payload, EqAdjustBandPayload>) {
            appendStringField(out, "payload", "EqAdjustBand");
            out << ",\"eq_band_index\":" << data.eq_band_index << ",\"eq_gain_delta\":" << data.eq_gain_delta;
        } else if constexpr (std::is_same_v<Payload, EqCyclePresetPayload>) {
            appendStringField(out, "payload", "EqCyclePreset");
            out << ",\"preset_delta\":" << data.preset_delta;
        } else if constexpr (std::is_same_v<Payload, EqApplyPresetPayload>) {
            appendStringField(out, "payload", "EqApplyPreset");
            appendStringField(out, "preset_name", data.preset_name);
        } else if constexpr (std::is_same_v<Payload, SettingsApplyLivePayload>) {
            appendStringField(out, "payload", "SettingsApplyLive");
            appendStringField(out, "output_mode", data.output_mode);
            appendStringField(out, "network_policy", data.network_policy);
            appendStringField(out, "sleep_timer", data.sleep_timer);
        } else if constexpr (std::is_same_v<Payload, RemotePlayResolvedStreamPayload>) {
            appendStringField(out, "payload", "RemotePlayResolvedStream");
            appendStringField(out, "source", data.source);
        } else if constexpr (std::is_same_v<Payload, RemotePlayResolvedLibraryTrackPayload>) {
            appendStringField(out, "payload", "RemotePlayResolvedLibraryTrack");
            out << ",\"track_id\":" << data.local_track_id;
            appendStringField(out, "source", data.source);
        }
    }, payload.data);
}

RuntimeCommandPayload parsePayload(std::string_view json)
{
    const auto payload = stringField(json, "payload").value_or("Empty");
    if (payload == "PlaybackStartTrack") {
        return RuntimeCommandPayload::startTrack(
            numberField<int>(json, "track_id").value_or(0),
            stringField(json, "album").value_or(std::string{}),
            stringField(json, "artist").value_or(std::string{}),
            stringField(json, "genre").value_or(std::string{}));
    }
    if (payload == "PlaybackSeek") {
        return RuntimeCommandPayload::seek(numberField<double>(json, "seconds").value_or(0.0));
    }
    if (payload == "QueueStep") {
        return RuntimeCommandPayload::queueStep(numberField<int>(json, "delta").value_or(0));
    }
    if (payload == "QueueIndex") {
        return RuntimeCommandPayload::queueIndex(numberField<int>(json, "queue_index").value_or(0));
    }
    if (payload == "RuntimeEnabled") {
        return RuntimeCommandPayload::enabled(boolField(json, "enabled").value_or(false));
    }
    if (payload == "RemoteTrackRef") {
        return RuntimeCommandPayload::remoteTrackRef(
            stringField(json, "profile_id").value_or(std::string{}),
            stringField(json, "item_id").value_or(std::string{}));
    }
    if (payload == "EqSetBand") {
        return RuntimeCommandPayload::eqSetBand(numberField<int>(json, "eq_band_index").value_or(0), numberField<int>(json, "eq_gain_db").value_or(0));
    }
    if (payload == "EqAdjustBand") {
        return RuntimeCommandPayload::eqAdjustBand(numberField<int>(json, "eq_band_index").value_or(0), numberField<int>(json, "eq_gain_delta").value_or(0));
    }
    if (payload == "EqCyclePreset") {
        return RuntimeCommandPayload::eqCyclePreset(numberField<int>(json, "preset_delta").value_or(0));
    }
    if (payload == "EqApplyPreset") {
        return RuntimeCommandPayload::eqApplyPreset(stringField(json, "preset_name").value_or(std::string{}));
    }
    if (payload == "SettingsApplyLive") {
        return RuntimeCommandPayload::settingsApplyLive(
            stringField(json, "output_mode").value_or(std::string{}),
            stringField(json, "network_policy").value_or(std::string{}),
            stringField(json, "sleep_timer").value_or(std::string{}));
    }
    return RuntimeCommandPayload::empty();
}

void appendResult(std::ostringstream& out, const RuntimeCommandResult& result)
{
    appendBoolField(out, "accepted", result.accepted);
    appendBoolField(out, "applied", result.applied);
    appendStringField(out, "code", result.code);
    appendStringField(out, "message", result.message);
    appendStringField(out, "origin", commandOriginName(result.origin));
    appendStringField(out, "correlation_id", result.correlation_id);
    out << ",\"version_before_apply\":" << result.version_before_apply
        << ",\"version_after_apply\":" << result.version_after_apply;
}

void appendSnapshot(std::ostringstream& out, const RuntimeSnapshot& snapshot)
{
    out << ",\"version\":" << snapshot.version;
    appendStringField(out, "playback_status", snapshot.playback.status == RuntimePlaybackStatus::Playing ? "Playing" : (snapshot.playback.status == RuntimePlaybackStatus::Paused ? "Paused" : "Empty"));
    out << ",\"current_track_id\":" << snapshot.playback.current_track_id.value_or(0);
    appendStringField(out, "title", snapshot.playback.title);
    appendStringField(out, "artist", snapshot.playback.artist);
    appendStringField(out, "album", snapshot.playback.album);
    appendStringField(out, "album_artist", snapshot.playback.album_artist);
    appendStringField(out, "source_label", snapshot.playback.source_label);
    appendStringField(out, "source_type", snapshot.playback.source_type);
    out << ",\"elapsed_seconds\":" << snapshot.playback.elapsed_seconds
        << ",\"duration_seconds\":" << snapshot.playback.duration_seconds;
    appendBoolField(out, "seekable", snapshot.playback.seekable);
    appendBoolField(out, "live", snapshot.playback.live);
    appendBoolField(out, "audio_active", snapshot.playback.audio_active);
    out << ",\"volume_percent\":" << snapshot.playback.volume_percent;
    appendBoolField(out, "muted", snapshot.playback.muted);
    appendStringField(out, "codec", snapshot.playback.codec);
    out << ",\"bitrate_kbps\":" << snapshot.playback.bitrate_kbps
        << ",\"sample_rate_hz\":" << snapshot.playback.sample_rate_hz
        << ",\"bit_depth\":" << snapshot.playback.bit_depth;
    appendBoolField(out, "shuffle_enabled", snapshot.playback.shuffle_enabled);
    appendBoolField(out, "repeat_all", snapshot.playback.repeat_all);
    appendBoolField(out, "repeat_one", snapshot.playback.repeat_one);
    appendStringField(out, "error_code", snapshot.playback.error_code);
    appendStringField(out, "error_message", snapshot.playback.error_message);
    appendIntArray(out, "queue_active_ids", snapshot.queue.active_ids);
    out << ",\"queue_active_index\":" << snapshot.queue.active_index;
    out << ",\"queue_selected_index\":" << snapshot.queue.selected_index;
    std::vector<int> queue_item_ids{};
    std::vector<int> queue_item_indexes{};
    std::vector<int> queue_item_durations{};
    std::vector<std::string> queue_item_titles{};
    std::vector<std::string> queue_item_artists{};
    std::vector<std::string> queue_item_albums{};
    std::vector<std::string> queue_item_sources{};
    for (const auto& item : snapshot.queue.visible_items) {
        queue_item_ids.push_back(item.track_id);
        queue_item_indexes.push_back(item.queue_index);
        queue_item_durations.push_back(item.duration_seconds);
        queue_item_titles.push_back(item.title);
        queue_item_artists.push_back(item.artist);
        queue_item_albums.push_back(item.album);
        queue_item_sources.push_back(item.source_label);
    }
    appendIntArray(out, "queue_item_ids", queue_item_ids);
    appendIntArray(out, "queue_item_indexes", queue_item_indexes);
    appendIntArray(out, "queue_item_durations", queue_item_durations);
    appendStringArray(out, "queue_item_titles", queue_item_titles);
    appendStringArray(out, "queue_item_artists", queue_item_artists);
    appendStringArray(out, "queue_item_albums", queue_item_albums);
    appendStringArray(out, "queue_item_sources", queue_item_sources);
    appendIntArray(out, "eq_bands", snapshot.eq.bands);
    appendStringField(out, "eq_preset_name", snapshot.eq.preset_name);
    appendBoolField(out, "eq_enabled", snapshot.eq.enabled);
    appendStringField(out, "remote_profile_id", snapshot.remote.profile_id);
    appendStringField(out, "remote_source_label", snapshot.remote.source_label);
    appendStringField(out, "remote_connection_status", snapshot.remote.connection_status);
    appendBoolField(out, "remote_stream_resolved", snapshot.remote.stream_resolved);
    appendStringField(out, "remote_redacted_url", snapshot.remote.redacted_url);
    appendStringField(out, "remote_buffer_state", snapshot.remote.buffer_state);
    appendStringField(out, "remote_recovery_action", snapshot.remote.recovery_action);
    out << ",\"remote_bitrate_kbps\":" << snapshot.remote.bitrate_kbps;
    appendStringField(out, "remote_codec", snapshot.remote.codec);
    appendBoolField(out, "remote_live", snapshot.remote.live);
    appendBoolField(out, "remote_seekable", snapshot.remote.seekable);
    appendStringField(out, "settings_output_mode", snapshot.settings.output_mode);
    appendStringField(out, "settings_network_policy", snapshot.settings.network_policy);
    appendStringField(out, "settings_sleep_timer", snapshot.settings.sleep_timer);
    appendBoolField(out, "visualization_available", snapshot.visualization.available);
    appendFloatArray(out, "visualization_bands", snapshot.visualization.bands);
    appendFloatArray(out, "visualization_peaks", snapshot.visualization.peaks);
    out << ",\"visualization_rms_left\":" << snapshot.visualization.rms_left
        << ",\"visualization_rms_right\":" << snapshot.visualization.rms_right
        << ",\"visualization_peak_left\":" << snapshot.visualization.peak_left
        << ",\"visualization_peak_right\":" << snapshot.visualization.peak_right;
    appendStringField(out, "visualization_mode", snapshot.visualization.mode);
    out << ",\"visualization_frame_index\":" << snapshot.visualization.frame_index;
    appendBoolField(out, "lyrics_available", snapshot.lyrics.available);
    appendBoolField(out, "lyrics_synced", snapshot.lyrics.synced);
    appendStringField(out, "lyrics_source", snapshot.lyrics.source);
    appendStringField(out, "lyrics_provider", snapshot.lyrics.provider);
    appendStringField(out, "lyrics_match_confidence", snapshot.lyrics.match_confidence);
    out << ",\"lyrics_current_index\":" << snapshot.lyrics.current_index
        << ",\"lyrics_offset_seconds\":" << snapshot.lyrics.offset_seconds;
    std::vector<int> lyric_indexes{};
    std::vector<std::string> lyric_texts{};
    for (const auto& line : snapshot.lyrics.visible_lines) {
        lyric_indexes.push_back(line.index);
        lyric_texts.push_back(line.text);
    }
    appendIntArray(out, "lyrics_line_indexes", lyric_indexes);
    appendStringArray(out, "lyrics_line_texts", lyric_texts);
    appendStringField(out, "lyrics_status_message", snapshot.lyrics.status_message);
    appendBoolField(out, "library_ready", snapshot.library.ready);
    appendBoolField(out, "library_degraded", snapshot.library.degraded);
    out << ",\"library_track_count\":" << snapshot.library.track_count
        << ",\"library_album_count\":" << snapshot.library.album_count
        << ",\"library_artist_count\":" << snapshot.library.artist_count
        << ",\"library_genre_count\":" << snapshot.library.genre_count;
    appendStringField(out, "library_status", snapshot.library.status);
    out << ",\"source_configured_count\":" << snapshot.sources.configured_count;
    appendStringField(out, "source_active_profile_id", snapshot.sources.active_profile_id);
    appendStringField(out, "source_active_label", snapshot.sources.active_source_label);
    appendStringField(out, "source_connection_status", snapshot.sources.connection_status);
    appendBoolField(out, "source_stream_resolved", snapshot.sources.stream_resolved);
    appendBoolField(out, "diagnostics_runtime_ok", snapshot.diagnostics.runtime_ok);
    appendBoolField(out, "diagnostics_audio_ok", snapshot.diagnostics.audio_ok);
    appendBoolField(out, "diagnostics_library_ok", snapshot.diagnostics.library_ok);
    appendBoolField(out, "diagnostics_remote_ok", snapshot.diagnostics.remote_ok);
    appendBoolField(out, "diagnostics_cache_ok", snapshot.diagnostics.cache_ok);
    appendStringField(out, "diagnostics_runtime_socket", snapshot.diagnostics.runtime_socket);
    appendStringField(out, "diagnostics_audio_backend", snapshot.diagnostics.audio_backend);
    appendStringField(out, "diagnostics_output_device", snapshot.diagnostics.output_device);
    appendStringField(out, "diagnostics_log_path", snapshot.diagnostics.log_path);
    out << ",\"diagnostics_library_track_count\":" << snapshot.diagnostics.library_track_count
        << ",\"diagnostics_failed_scan_count\":" << snapshot.diagnostics.failed_scan_count;
    appendStringArray(out, "diagnostics_warnings", snapshot.diagnostics.warnings);
    appendStringArray(out, "diagnostics_errors", snapshot.diagnostics.errors);
    appendBoolField(out, "creator_available", snapshot.creator.available);
    out << ",\"creator_bpm\":" << snapshot.creator.bpm
        << ",\"creator_lufs\":" << snapshot.creator.lufs
        << ",\"creator_dynamic_range\":" << snapshot.creator.dynamic_range;
    appendStringField(out, "creator_key", snapshot.creator.key);
    appendBoolField(out, "creator_waveform_available", snapshot.creator.waveform_available);
    appendFloatArray(out, "creator_waveform_points", snapshot.creator.waveform_points);
    appendBoolField(out, "creator_beat_grid_available", snapshot.creator.beat_grid_available);
    appendDoubleArray(out, "creator_beat_grid_seconds", snapshot.creator.beat_grid_seconds);
    appendDoubleArray(out, "creator_loop_marker_seconds", snapshot.creator.loop_marker_seconds);
    appendStringArray(out, "creator_section_markers", snapshot.creator.section_markers);
    appendStringField(out, "creator_stem_status", snapshot.creator.stem_status);
    appendStringField(out, "creator_analysis_source", snapshot.creator.analysis_source);
    appendStringField(out, "creator_confidence", snapshot.creator.confidence);
    appendStringField(out, "creator_status_message", snapshot.creator.status_message);
    out << ",\"plugins_loaded_count\":" << snapshot.plugins.loaded_count;
    appendStringField(out, "plugins_selected_skin_id", snapshot.plugins.selected_skin_id);
    appendStringArray(out, "plugins_loaded_plugin_ids", snapshot.plugins.loaded_plugin_ids);
    appendStringArray(out, "plugins_warnings", snapshot.plugins.warnings);
}

RuntimeSnapshot parseSnapshot(std::string_view json)
{
    RuntimeSnapshot snapshot{};
    snapshot.version = numberField<std::uint64_t>(json, "version").value_or(0);
    const auto status = stringField(json, "playback_status").value_or("Empty");
    snapshot.playback.status = status == "Playing" ? RuntimePlaybackStatus::Playing : (status == "Paused" ? RuntimePlaybackStatus::Paused : RuntimePlaybackStatus::Empty);
    if (const auto track_id = numberField<int>(json, "current_track_id"); track_id && *track_id > 0) {
        snapshot.playback.current_track_id = *track_id;
    }
    snapshot.playback.title = stringField(json, "title").value_or(std::string{});
    snapshot.playback.artist = stringField(json, "artist").value_or(std::string{});
    snapshot.playback.album = stringField(json, "album").value_or(std::string{});
    snapshot.playback.album_artist = stringField(json, "album_artist").value_or(std::string{});
    snapshot.playback.source_label = stringField(json, "source_label").value_or(std::string{});
    snapshot.playback.source_type = stringField(json, "source_type").value_or(std::string{});
    snapshot.playback.elapsed_seconds = numberField<double>(json, "elapsed_seconds").value_or(0.0);
    snapshot.playback.duration_seconds = numberField<int>(json, "duration_seconds").value_or(0);
    snapshot.playback.seekable = boolField(json, "seekable").value_or(true);
    snapshot.playback.live = boolField(json, "live").value_or(false);
    snapshot.playback.audio_active = boolField(json, "audio_active").value_or(false);
    snapshot.playback.volume_percent = numberField<int>(json, "volume_percent").value_or(100);
    snapshot.playback.muted = boolField(json, "muted").value_or(false);
    snapshot.playback.codec = stringField(json, "codec").value_or(std::string{});
    snapshot.playback.bitrate_kbps = numberField<int>(json, "bitrate_kbps").value_or(0);
    snapshot.playback.sample_rate_hz = numberField<int>(json, "sample_rate_hz").value_or(0);
    snapshot.playback.bit_depth = numberField<int>(json, "bit_depth").value_or(0);
    snapshot.playback.shuffle_enabled = boolField(json, "shuffle_enabled").value_or(false);
    snapshot.playback.repeat_all = boolField(json, "repeat_all").value_or(false);
    snapshot.playback.repeat_one = boolField(json, "repeat_one").value_or(false);
    snapshot.playback.error_code = stringField(json, "error_code").value_or(std::string{});
    snapshot.playback.error_message = stringField(json, "error_message").value_or(std::string{});
    snapshot.queue.active_ids = intArrayField(json, "queue_active_ids");
    snapshot.queue.active_index = numberField<int>(json, "queue_active_index").value_or(-1);
    snapshot.queue.selected_index = numberField<int>(json, "queue_selected_index").value_or(snapshot.queue.active_index);
    const auto queue_item_ids = intArrayField(json, "queue_item_ids");
    const auto queue_item_indexes = intArrayField(json, "queue_item_indexes");
    const auto queue_item_durations = intArrayField(json, "queue_item_durations");
    const auto queue_item_titles = stringArrayField(json, "queue_item_titles");
    const auto queue_item_artists = stringArrayField(json, "queue_item_artists");
    const auto queue_item_albums = stringArrayField(json, "queue_item_albums");
    const auto queue_item_sources = stringArrayField(json, "queue_item_sources");
    for (std::size_t index = 0; index < queue_item_ids.size(); ++index) {
        RuntimeQueueItem item{};
        item.track_id = queue_item_ids[index];
        item.queue_index = index < queue_item_indexes.size() ? queue_item_indexes[index] : static_cast<int>(index);
        item.duration_seconds = index < queue_item_durations.size() ? queue_item_durations[index] : 0;
        item.title = index < queue_item_titles.size() ? queue_item_titles[index] : std::string{};
        item.artist = index < queue_item_artists.size() ? queue_item_artists[index] : std::string{};
        item.album = index < queue_item_albums.size() ? queue_item_albums[index] : std::string{};
        item.source_label = index < queue_item_sources.size() ? queue_item_sources[index] : std::string{};
        item.active = item.queue_index == snapshot.queue.active_index;
        snapshot.queue.visible_items.push_back(std::move(item));
    }
    const auto bands = intArrayField(json, "eq_bands");
    for (std::size_t index = 0; index < snapshot.eq.bands.size() && index < bands.size(); ++index) {
        snapshot.eq.bands[index] = bands[index];
    }
    snapshot.eq.preset_name = stringField(json, "eq_preset_name").value_or("FLAT");
    snapshot.eq.enabled = boolField(json, "eq_enabled").value_or(false);
    snapshot.remote.profile_id = stringField(json, "remote_profile_id").value_or(std::string{});
    snapshot.remote.source_label = stringField(json, "remote_source_label").value_or("REMOTE");
    snapshot.remote.connection_status = stringField(json, "remote_connection_status").value_or("UNKNOWN");
    snapshot.remote.stream_resolved = boolField(json, "remote_stream_resolved").value_or(false);
    snapshot.remote.redacted_url = stringField(json, "remote_redacted_url").value_or(std::string{});
    snapshot.remote.buffer_state = stringField(json, "remote_buffer_state").value_or("UNKNOWN");
    snapshot.remote.recovery_action = stringField(json, "remote_recovery_action").value_or("NONE");
    snapshot.remote.bitrate_kbps = numberField<int>(json, "remote_bitrate_kbps").value_or(0);
    snapshot.remote.codec = stringField(json, "remote_codec").value_or(std::string{});
    snapshot.remote.live = boolField(json, "remote_live").value_or(false);
    snapshot.remote.seekable = boolField(json, "remote_seekable").value_or(false);
    snapshot.settings.output_mode = stringField(json, "settings_output_mode").value_or("DEFAULT");
    snapshot.settings.network_policy = stringField(json, "settings_network_policy").value_or("DEFAULT");
    snapshot.settings.sleep_timer = stringField(json, "settings_sleep_timer").value_or("OFF");
    snapshot.visualization.available = boolField(json, "visualization_available").value_or(false);
    const auto visualization_bands = numberArrayField(json, "visualization_bands");
    for (std::size_t index = 0; index < snapshot.visualization.bands.size() && index < visualization_bands.size(); ++index) {
        snapshot.visualization.bands[index] = static_cast<float>(visualization_bands[index]);
    }
    const auto visualization_peaks = numberArrayField(json, "visualization_peaks");
    for (std::size_t index = 0; index < snapshot.visualization.peaks.size() && index < visualization_peaks.size(); ++index) {
        snapshot.visualization.peaks[index] = static_cast<float>(visualization_peaks[index]);
    }
    snapshot.visualization.rms_left = static_cast<float>(numberField<double>(json, "visualization_rms_left").value_or(0.0));
    snapshot.visualization.rms_right = static_cast<float>(numberField<double>(json, "visualization_rms_right").value_or(0.0));
    snapshot.visualization.peak_left = static_cast<float>(numberField<double>(json, "visualization_peak_left").value_or(0.0));
    snapshot.visualization.peak_right = static_cast<float>(numberField<double>(json, "visualization_peak_right").value_or(0.0));
    snapshot.visualization.mode = stringField(json, "visualization_mode").value_or("spectrum");
    snapshot.visualization.frame_index = numberField<std::uint64_t>(json, "visualization_frame_index").value_or(0);
    snapshot.lyrics.available = boolField(json, "lyrics_available").value_or(false);
    snapshot.lyrics.synced = boolField(json, "lyrics_synced").value_or(false);
    snapshot.lyrics.source = stringField(json, "lyrics_source").value_or(std::string{});
    snapshot.lyrics.provider = stringField(json, "lyrics_provider").value_or(std::string{});
    snapshot.lyrics.match_confidence = stringField(json, "lyrics_match_confidence").value_or(std::string{});
    snapshot.lyrics.current_index = numberField<int>(json, "lyrics_current_index").value_or(-1);
    snapshot.lyrics.offset_seconds = numberField<double>(json, "lyrics_offset_seconds").value_or(0.0);
    const auto lyric_indexes = intArrayField(json, "lyrics_line_indexes");
    const auto lyric_texts = stringArrayField(json, "lyrics_line_texts");
    for (std::size_t index = 0; index < lyric_texts.size(); ++index) {
        RuntimeLyricLine line{};
        line.index = index < lyric_indexes.size() ? lyric_indexes[index] : static_cast<int>(index);
        line.text = lyric_texts[index];
        line.current = line.index == snapshot.lyrics.current_index;
        snapshot.lyrics.visible_lines.push_back(std::move(line));
    }
    snapshot.lyrics.status_message = stringField(json, "lyrics_status_message").value_or(std::string{});
    snapshot.library.ready = boolField(json, "library_ready").value_or(false);
    snapshot.library.degraded = boolField(json, "library_degraded").value_or(false);
    snapshot.library.track_count = numberField<int>(json, "library_track_count").value_or(0);
    snapshot.library.album_count = numberField<int>(json, "library_album_count").value_or(0);
    snapshot.library.artist_count = numberField<int>(json, "library_artist_count").value_or(0);
    snapshot.library.genre_count = numberField<int>(json, "library_genre_count").value_or(0);
    snapshot.library.status = stringField(json, "library_status").value_or("UNINITIALIZED");
    snapshot.sources.configured_count = numberField<int>(json, "source_configured_count").value_or(0);
    snapshot.sources.active_profile_id = stringField(json, "source_active_profile_id").value_or(std::string{});
    snapshot.sources.active_source_label = stringField(json, "source_active_label").value_or(std::string{});
    snapshot.sources.connection_status = stringField(json, "source_connection_status").value_or("UNKNOWN");
    snapshot.sources.stream_resolved = boolField(json, "source_stream_resolved").value_or(false);
    snapshot.diagnostics.runtime_ok = boolField(json, "diagnostics_runtime_ok").value_or(true);
    snapshot.diagnostics.audio_ok = boolField(json, "diagnostics_audio_ok").value_or(true);
    snapshot.diagnostics.library_ok = boolField(json, "diagnostics_library_ok").value_or(true);
    snapshot.diagnostics.remote_ok = boolField(json, "diagnostics_remote_ok").value_or(true);
    snapshot.diagnostics.cache_ok = boolField(json, "diagnostics_cache_ok").value_or(true);
    snapshot.diagnostics.runtime_socket = stringField(json, "diagnostics_runtime_socket").value_or(std::string{});
    snapshot.diagnostics.audio_backend = stringField(json, "diagnostics_audio_backend").value_or(std::string{});
    snapshot.diagnostics.output_device = stringField(json, "diagnostics_output_device").value_or(std::string{});
    snapshot.diagnostics.log_path = stringField(json, "diagnostics_log_path").value_or(std::string{});
    snapshot.diagnostics.library_track_count = numberField<int>(json, "diagnostics_library_track_count").value_or(0);
    snapshot.diagnostics.failed_scan_count = numberField<int>(json, "diagnostics_failed_scan_count").value_or(0);
    snapshot.diagnostics.warnings = stringArrayField(json, "diagnostics_warnings");
    snapshot.diagnostics.errors = stringArrayField(json, "diagnostics_errors");
    snapshot.creator.available = boolField(json, "creator_available").value_or(false);
    snapshot.creator.bpm = numberField<double>(json, "creator_bpm").value_or(0.0);
    snapshot.creator.lufs = numberField<double>(json, "creator_lufs").value_or(0.0);
    snapshot.creator.dynamic_range = numberField<double>(json, "creator_dynamic_range").value_or(0.0);
    snapshot.creator.key = stringField(json, "creator_key").value_or(std::string{});
    snapshot.creator.waveform_available = boolField(json, "creator_waveform_available").value_or(false);
    const auto waveform = numberArrayField(json, "creator_waveform_points");
    for (const auto value : waveform) {
        snapshot.creator.waveform_points.push_back(static_cast<float>(value));
    }
    snapshot.creator.beat_grid_available = boolField(json, "creator_beat_grid_available").value_or(false);
    snapshot.creator.beat_grid_seconds = numberArrayField(json, "creator_beat_grid_seconds");
    snapshot.creator.loop_marker_seconds = numberArrayField(json, "creator_loop_marker_seconds");
    snapshot.creator.section_markers = stringArrayField(json, "creator_section_markers");
    snapshot.creator.stem_status = stringField(json, "creator_stem_status").value_or("not-separated");
    snapshot.creator.analysis_source = stringField(json, "creator_analysis_source").value_or(std::string{});
    snapshot.creator.confidence = stringField(json, "creator_confidence").value_or(std::string{});
    snapshot.creator.status_message = stringField(json, "creator_status_message").value_or("Creator analysis unavailable");
    snapshot.plugins.loaded_count = numberField<int>(json, "plugins_loaded_count").value_or(0);
    snapshot.plugins.selected_skin_id = stringField(json, "plugins_selected_skin_id").value_or(std::string{});
    snapshot.plugins.loaded_plugin_ids = stringArrayField(json, "plugins_loaded_plugin_ids");
    snapshot.plugins.warnings = stringArrayField(json, "plugins_warnings");
    if (snapshot.plugins.loaded_count == 0 && !snapshot.plugins.loaded_plugin_ids.empty()) {
        snapshot.plugins.loaded_count = static_cast<int>(snapshot.plugins.loaded_plugin_ids.size());
    }
    return snapshot;
}

} // namespace

std::string runtimeCommandKindName(RuntimeCommandKind kind) noexcept
{
    return std::string{enumName(kind, kCommandNames)};
}

std::optional<RuntimeCommandKind> runtimeCommandKindFromName(std::string_view name) noexcept
{
    return enumFromName(name, kCommandNames);
}

std::string runtimeQueryKindName(RuntimeQueryKind kind) noexcept
{
    return std::string{enumName(kind, kQueryNames)};
}

std::optional<RuntimeQueryKind> runtimeQueryKindFromName(std::string_view name) noexcept
{
    return enumFromName(name, kQueryNames);
}

std::string commandOriginName(CommandOrigin origin) noexcept
{
    return std::string{enumName(origin, kOriginNames)};
}

std::optional<CommandOrigin> commandOriginFromName(std::string_view name) noexcept
{
    return enumFromName(name, kOriginNames);
}

std::string serializeRuntimeCommandRequest(const RuntimeCommandRequest& request)
{
    std::ostringstream out;
    out << "{\"type\":\"command\"";
    appendStringField(out, "kind", runtimeCommandKindName(request.command.kind));
    appendStringField(out, "origin", commandOriginName(request.command.origin));
    appendStringField(out, "correlation_id", request.command.correlation_id);
    appendPayload(out, request.command.payload);
    out << '}';
    return out.str();
}

std::string serializeRuntimeCommandResponse(const RuntimeCommandResponse& response)
{
    std::ostringstream out;
    out << "{\"type\":\"command_response\"";
    appendResult(out, response.result);
    out << '}';
    return out.str();
}

std::string serializeRuntimeQueryRequest(const RuntimeQueryRequest& request)
{
    std::ostringstream out;
    out << "{\"type\":\"query\"";
    appendStringField(out, "kind", runtimeQueryKindName(request.query.kind));
    appendStringField(out, "origin", commandOriginName(request.query.origin));
    appendStringField(out, "correlation_id", request.query.correlation_id);
    out << '}';
    return out.str();
}

std::string serializeRuntimeQueryResponse(const RuntimeQueryResponse& response)
{
    std::ostringstream out;
    out << "{\"type\":\"query_response\"";
    appendSnapshot(out, response.snapshot);
    out << '}';
    return out.str();
}

std::string serializeRuntimeEventStreamRequest(const RuntimeEventStreamRequest& request)
{
    std::ostringstream out;
    out << "{\"type\":\"event_stream\"";
    appendStringField(out, "kind", runtimeQueryKindName(request.query.kind));
    appendStringField(out, "origin", commandOriginName(request.query.origin));
    appendStringField(out, "correlation_id", request.query.correlation_id);
    out << ",\"after_version\":" << request.after_version
        << ",\"heartbeat_ms\":" << request.heartbeat_ms
        << ",\"max_events\":" << request.max_events;
    out << '}';
    return out.str();
}

std::string serializeRuntimeEvent(const RuntimeEvent& event)
{
    std::ostringstream out;
    out << "{\"type\":\"event\"";
    appendStringField(out, "event_type", runtimeEventKindName(event.kind));
    out << ",\"event_version\":" << event.version
        << ",\"timestamp_ms\":" << event.timestamp_ms
        << ",\"payload_elapsed_seconds\":" << event.elapsed_seconds
        << ",\"payload_duration_seconds\":" << event.duration_seconds
        << ",\"payload_current_index\":" << event.current_index
        << ",\"payload_offset_seconds\":" << event.offset_seconds;
    appendStringField(out, "payload_text", event.text);
    appendStringField(out, "payload_code", event.code);
    appendStringField(out, "payload_message", event.message);
    appendSnapshot(out, event.snapshot);
    out << '}';
    return out.str();
}

std::optional<RuntimeCommandRequest> parseRuntimeCommandRequest(std::string_view json)
{
    const auto kind_name = stringField(json, "kind");
    const auto kind = kind_name ? runtimeCommandKindFromName(*kind_name) : std::nullopt;
    if (!kind) {
        return std::nullopt;
    }
    const auto origin_name = stringField(json, "origin");
    return RuntimeCommandRequest{RuntimeCommand{
        *kind,
        parsePayload(json),
        origin_name ? commandOriginFromName(*origin_name).value_or(CommandOrigin::Unknown) : CommandOrigin::Unknown,
        stringField(json, "correlation_id").value_or(std::string{})}};
}

std::optional<RuntimeCommandResponse> parseRuntimeCommandResponse(std::string_view json)
{
    RuntimeCommandResult result{};
    result.accepted = boolField(json, "accepted").value_or(false);
    result.applied = boolField(json, "applied").value_or(false);
    result.code = stringField(json, "code").value_or(std::string{});
    result.message = stringField(json, "message").value_or(std::string{});
    if (const auto origin = stringField(json, "origin")) {
        result.origin = commandOriginFromName(*origin).value_or(CommandOrigin::Unknown);
    }
    result.correlation_id = stringField(json, "correlation_id").value_or(std::string{});
    result.version_before_apply = numberField<std::uint64_t>(json, "version_before_apply").value_or(0);
    result.version_after_apply = numberField<std::uint64_t>(json, "version_after_apply").value_or(0);
    return RuntimeCommandResponse{result};
}

std::optional<RuntimeQueryRequest> parseRuntimeQueryRequest(std::string_view json)
{
    const auto kind_name = stringField(json, "kind");
    const auto kind = kind_name ? runtimeQueryKindFromName(*kind_name) : std::nullopt;
    if (!kind) {
        return std::nullopt;
    }
    const auto origin_name = stringField(json, "origin");
    return RuntimeQueryRequest{RuntimeQuery{
        *kind,
        origin_name ? commandOriginFromName(*origin_name).value_or(CommandOrigin::Unknown) : CommandOrigin::Unknown,
        stringField(json, "correlation_id").value_or(std::string{})}};
}

std::optional<RuntimeQueryResponse> parseRuntimeQueryResponse(std::string_view json)
{
    return RuntimeQueryResponse{parseSnapshot(json)};
}

std::optional<RuntimeEventStreamRequest> parseRuntimeEventStreamRequest(std::string_view json)
{
    const auto type = stringField(json, "type");
    if (!type || *type != "event_stream") {
        return std::nullopt;
    }
    const auto kind_name = stringField(json, "kind");
    const auto kind = kind_name ? runtimeQueryKindFromName(*kind_name) : std::optional<RuntimeQueryKind>{RuntimeQueryKind::FullSnapshot};
    if (!kind) {
        return std::nullopt;
    }
    const auto origin_name = stringField(json, "origin");
    RuntimeEventStreamRequest request{};
    request.query = RuntimeQuery{
        *kind,
        origin_name ? commandOriginFromName(*origin_name).value_or(CommandOrigin::Unknown) : CommandOrigin::Unknown,
        stringField(json, "correlation_id").value_or(std::string{})};
    request.after_version = numberField<std::uint64_t>(json, "after_version").value_or(0);
    request.heartbeat_ms = numberField<int>(json, "heartbeat_ms").value_or(1000);
    request.max_events = numberField<int>(json, "max_events").value_or(0);
    return request;
}

std::optional<RuntimeEvent> parseRuntimeEvent(std::string_view json)
{
    const auto type = stringField(json, "type");
    if (!type || *type != "event") {
        return std::nullopt;
    }
    const auto event_type = stringField(json, "event_type");
    const auto kind = event_type ? runtimeEventKindFromName(*event_type) : std::nullopt;
    if (!kind) {
        return std::nullopt;
    }
    RuntimeEvent event{};
    event.kind = *kind;
    event.version = numberField<std::uint64_t>(json, "event_version").value_or(0);
    event.timestamp_ms = numberField<std::uint64_t>(json, "timestamp_ms").value_or(0);
    event.elapsed_seconds = numberField<double>(json, "payload_elapsed_seconds").value_or(0.0);
    event.duration_seconds = numberField<int>(json, "payload_duration_seconds").value_or(0);
    event.current_index = numberField<int>(json, "payload_current_index").value_or(-1);
    event.offset_seconds = numberField<double>(json, "payload_offset_seconds").value_or(0.0);
    event.text = stringField(json, "payload_text").value_or(std::string{});
    event.code = stringField(json, "payload_code").value_or(std::string{});
    event.message = stringField(json, "payload_message").value_or(std::string{});
    event.snapshot = parseSnapshot(json);
    return event;
}

} // namespace lofibox::runtime
