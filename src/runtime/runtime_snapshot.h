// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace lofibox::runtime {

enum class RuntimePlaybackStatus {
    Empty,
    Paused,
    Playing,
};

struct PlaybackRuntimeSnapshot {
    RuntimePlaybackStatus status{RuntimePlaybackStatus::Empty};
    std::optional<int> current_track_id{};
    std::string title{};
    std::string artist{};
    std::string album{};
    std::string album_artist{};
    std::string source_label{};
    std::string source_type{};
    double elapsed_seconds{0.0};
    int duration_seconds{0};
    bool seekable{true};
    bool live{false};
    bool audio_active{false};
    int volume_percent{100};
    bool muted{false};
    std::string codec{};
    int bitrate_kbps{0};
    int sample_rate_hz{0};
    int bit_depth{0};
    bool shuffle_enabled{false};
    bool repeat_all{false};
    bool repeat_one{false};
    std::string error_code{};
    std::string error_message{};
    std::uint64_t version{0};
};

struct RuntimeQueueItem {
    int track_id{-1};
    int queue_index{-1};
    std::string title{};
    std::string artist{};
    std::string album{};
    std::string source_label{};
    int duration_seconds{0};
    bool active{false};
    bool playable{true};
};

struct QueueRuntimeSnapshot {
    std::vector<int> active_ids{};
    std::vector<RuntimeQueueItem> visible_items{};
    int active_index{-1};
    int selected_index{-1};
    bool shuffle_enabled{false};
    bool repeat_all{false};
    bool repeat_one{false};
    std::uint64_t version{0};
};

struct EqRuntimeSnapshot {
    std::array<int, 10> bands{};
    std::string preset_name{"FLAT"};
    bool enabled{false};
    std::uint64_t version{0};
};

struct RemoteSessionSnapshot {
    std::string profile_id{};
    std::string source_label{"REMOTE"};
    std::string connection_status{"UNKNOWN"};
    bool stream_resolved{false};
    std::string redacted_url{};
    std::string buffer_state{"UNKNOWN"};
    std::string recovery_action{"NONE"};
    int bitrate_kbps{0};
    std::string codec{};
    bool live{false};
    bool seekable{false};
    std::uint64_t version{0};
};

struct SettingsRuntimeSnapshot {
    std::string output_mode{"DEFAULT"};
    std::string network_policy{"DEFAULT"};
    std::string sleep_timer{"OFF"};
    std::uint64_t version{0};
};

struct VisualizationRuntimeSnapshot {
    bool available{false};
    std::array<float, 10> bands{};
    std::array<float, 10> peaks{};
    float rms_left{0.0F};
    float rms_right{0.0F};
    float peak_left{0.0F};
    float peak_right{0.0F};
    std::string mode{"spectrum"};
    std::uint64_t frame_index{0};
    std::uint64_t version{0};
};

struct RuntimeLyricLine {
    int index{-1};
    double timestamp_seconds{0.0};
    std::string text{};
    std::string translation{};
    bool current{false};
};

struct LyricsRuntimeSnapshot {
    bool available{false};
    bool synced{false};
    std::string source{};
    std::string provider{};
    std::string match_confidence{};
    int current_index{-1};
    double offset_seconds{0.0};
    std::vector<RuntimeLyricLine> visible_lines{};
    std::string status_message{};
    std::uint64_t version{0};
};

struct LibraryRuntimeSnapshot {
    bool ready{false};
    bool degraded{false};
    int track_count{0};
    int album_count{0};
    int artist_count{0};
    int genre_count{0};
    std::string status{"UNINITIALIZED"};
    std::string scan_phase{"IDLE"};
    std::string scan_message{};
    std::string scan_current_path{};
    int scan_roots_total{0};
    int scan_roots_scanned{0};
    int scan_files_discovered{0};
    int scan_files_total{0};
    int scan_files_processed{0};
    int scan_tracks_indexed{0};
    std::uint64_t version{0};
};

struct SourceRuntimeSnapshot {
    int configured_count{0};
    std::string active_profile_id{};
    std::string active_source_label{};
    std::string connection_status{"UNKNOWN"};
    bool stream_resolved{false};
    std::uint64_t version{0};
};

struct DiagnosticsRuntimeSnapshot {
    bool runtime_ok{true};
    bool audio_ok{true};
    bool library_ok{true};
    bool remote_ok{true};
    bool cache_ok{true};
    std::string runtime_socket{};
    std::string audio_backend{};
    std::string output_device{};
    std::string log_path{};
    int library_track_count{0};
    int failed_scan_count{0};
    std::vector<std::string> warnings{};
    std::vector<std::string> errors{};
    std::uint64_t version{0};
};

struct CreatorRuntimeSnapshot {
    bool available{false};
    double bpm{0.0};
    std::string key{};
    double lufs{0.0};
    double dynamic_range{0.0};
    bool waveform_available{false};
    std::vector<float> waveform_points{};
    bool beat_grid_available{false};
    std::vector<double> beat_grid_seconds{};
    std::vector<double> loop_marker_seconds{};
    std::vector<std::string> section_markers{};
    std::string stem_status{"not-separated"};
    std::string analysis_source{};
    std::string confidence{};
    std::string status_message{"Creator analysis unavailable"};
    std::uint64_t version{0};
};

struct PluginRuntimeSnapshot {
    int loaded_count{0};
    std::string selected_skin_id{};
    std::vector<std::string> loaded_plugin_ids{};
    std::vector<std::string> warnings{};
    std::uint64_t version{0};
};

struct RuntimeSnapshot {
    PlaybackRuntimeSnapshot playback{};
    QueueRuntimeSnapshot queue{};
    EqRuntimeSnapshot eq{};
    RemoteSessionSnapshot remote{};
    SettingsRuntimeSnapshot settings{};
    VisualizationRuntimeSnapshot visualization{};
    LyricsRuntimeSnapshot lyrics{};
    LibraryRuntimeSnapshot library{};
    SourceRuntimeSnapshot sources{};
    DiagnosticsRuntimeSnapshot diagnostics{};
    CreatorRuntimeSnapshot creator{};
    PluginRuntimeSnapshot plugins{};
    std::uint64_t version{0};
};

} // namespace lofibox::runtime
