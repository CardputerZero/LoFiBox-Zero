// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstdint>
#include <array>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "app/audio_visualization.h"
#include "app/runtime_services.h"
#include "core/canvas.h"

namespace lofibox::app {

enum class PlaybackStatus {
    Empty,
    Paused,
    Playing,
};

struct QueueState {
    std::vector<int> base_ids{};
    std::vector<int> active_ids{};
    int active_index{-1};
};

struct PlaybackSession {
    PlaybackStatus status{PlaybackStatus::Empty};
    std::optional<int> current_track_id{};
    std::string current_stream_title{};
    std::string current_stream_source{};
    std::string current_stream_artist{};
    std::string current_stream_album{};
    int current_stream_duration_seconds{0};
    std::string current_stream_url_redacted{};
    std::string current_stream_artwork_url{};
    bool current_stream_live{false};
    bool shuffle_enabled{false};
    bool repeat_all{false};
    bool repeat_one{false};
    double elapsed_seconds{0.0};
    std::uint64_t transition_generation{0};
    bool audio_active{false};
    bool finish_pending{false};
    double finish_pending_seconds{0.0};
    bool lyrics_lookup_pending{false};
    std::optional<core::Canvas> current_artwork{};
    TrackLyrics current_lyrics{};
    bool visualization_pending{false};
    AudioVisualizationFrame visualization_frame{};
};

struct PlaybackEnrichmentResult {
    int track_id{0};
    std::filesystem::path path{};
    TrackMetadata metadata{};
    TrackIdentity identity{};
    std::optional<core::Canvas> artwork{};
    TrackLyrics lyrics{};
    std::uint64_t generation{0};
    bool remote{false};
    RemoteServerProfile remote_profile{};
    RemoteTrack remote_track{};
    bool cache_remote_facts{false};
};

} // namespace lofibox::app
