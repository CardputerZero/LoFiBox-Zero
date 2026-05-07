// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <string>
#include <utility>
#include <variant>

#include "app/remote_media_services.h"
#include "runtime/runtime_snapshot.h"

namespace lofibox::runtime {

enum class CommandOrigin {
    Gui,
    DirectTest,
    Desktop,
    RuntimeCli,
    Automation,
    Unknown,
};

enum class RuntimeCommandKind {
    PlaybackPlay,
    PlaybackPause,
    PlaybackResume,
    PlaybackToggle,
    PlaybackStartTrack,
    PlaybackStop,
    PlaybackSeek,
    QueueStep,
    QueueJump,
    QueueClear,
    PlaybackToggleShuffle,
    PlaybackSetShuffle,
    PlaybackCycleRepeat,
    PlaybackCycleMainMenuMode,
    PlaybackSetRepeatAll,
    PlaybackSetRepeatOne,
    RemoteResolveAndStartTrack,
    RemoteStartActiveStream,
    EqEnable,
    EqDisable,
    EqSetBand,
    EqAdjustBand,
    EqApplyPreset,
    EqCyclePreset,
    EqReset,
    AudioEffectCycle,
    RemoteReconnect,
    SettingsApplyLive,
    RuntimeShutdown,
    RuntimeReload,
};

enum class RuntimeQueryKind {
    PlaybackSnapshot,
    QueueSnapshot,
    EqSnapshot,
    RemoteSessionSnapshot,
    SettingsSnapshot,
    FullSnapshot,
};

struct EmptyPayload {};

struct PlaybackStartTrackPayload {
    int track_id{0};
    std::string album{};   // optional — sets SongsContext to this album
    std::string artist{};  // optional — disambiguates same-name albums
    std::string genre{};   // optional — sets SongsContext to this genre
};

struct PlaybackSeekPayload {
    double seconds{0.0};
};

struct QueueStepPayload {
    int delta{0};
};

struct QueueIndexPayload {
    int queue_index{0};
};

struct RuntimeEnabledPayload {
    bool enabled{false};
};

struct RemoteTrackRefPayload {
    std::string profile_id{};
    std::string item_id{};
};

struct EqSetBandPayload {
    int eq_band_index{0};
    int eq_gain_db{0};
};

struct EqAdjustBandPayload {
    int eq_band_index{0};
    int eq_gain_delta{0};
};

struct EqCyclePresetPayload {
    int preset_delta{0};
};

struct EqApplyPresetPayload {
    std::string preset_name{};
};

struct AudioEffectCyclePayload {
    std::string plugin_id{};
    int delta{1};
};

struct SettingsApplyLivePayload {
    std::string output_mode{};
    std::string network_policy{};
    std::string sleep_timer{};
};

struct RemotePlayResolvedStreamPayload {
    app::ResolvedRemoteStream stream{};
    app::RemoteTrack track{};
    std::string source{};
    RemoteSessionSnapshot snapshot{};
};

struct RemotePlayResolvedLibraryTrackPayload {
    int local_track_id{0};
    app::RemoteServerProfile profile{};
    app::RemoteTrack track{};
    app::ResolvedRemoteStream stream{};
    std::string source{};
    bool cache_remote_facts{false};
    RemoteSessionSnapshot snapshot{};
};

using RuntimeCommandData = std::variant<
    EmptyPayload,
    PlaybackStartTrackPayload,
    PlaybackSeekPayload,
    QueueStepPayload,
    QueueIndexPayload,
    RuntimeEnabledPayload,
    RemoteTrackRefPayload,
    EqSetBandPayload,
    EqAdjustBandPayload,
    EqCyclePresetPayload,
    EqApplyPresetPayload,
    AudioEffectCyclePayload,
    SettingsApplyLivePayload,
    RemotePlayResolvedStreamPayload,
    RemotePlayResolvedLibraryTrackPayload>;

struct RuntimeCommandPayload {
    RuntimeCommandData data{EmptyPayload{}};

    [[nodiscard]] static RuntimeCommandPayload empty() { return {}; }
    [[nodiscard]] static RuntimeCommandPayload startTrack(int track_id,
        std::string album = {}, std::string artist = {}, std::string genre = {})
    { return {{PlaybackStartTrackPayload{track_id, std::move(album), std::move(artist), std::move(genre)}}}; }
    [[nodiscard]] static RuntimeCommandPayload seek(double seconds) { return {{PlaybackSeekPayload{seconds}}}; }
    [[nodiscard]] static RuntimeCommandPayload queueStep(int delta) { return {{QueueStepPayload{delta}}}; }
    [[nodiscard]] static RuntimeCommandPayload queueIndex(int queue_index) { return {{QueueIndexPayload{queue_index}}}; }
    [[nodiscard]] static RuntimeCommandPayload enabled(bool value) { return {{RuntimeEnabledPayload{value}}}; }
    [[nodiscard]] static RuntimeCommandPayload remoteTrackRef(std::string profile_id, std::string item_id)
    {
        return {{RemoteTrackRefPayload{std::move(profile_id), std::move(item_id)}}};
    }
    [[nodiscard]] static RuntimeCommandPayload eqSetBand(int band_index, int gain_db) { return {{EqSetBandPayload{band_index, gain_db}}}; }
    [[nodiscard]] static RuntimeCommandPayload eqAdjustBand(int band_index, int delta_db) { return {{EqAdjustBandPayload{band_index, delta_db}}}; }
    [[nodiscard]] static RuntimeCommandPayload eqCyclePreset(int delta) { return {{EqCyclePresetPayload{delta}}}; }
    [[nodiscard]] static RuntimeCommandPayload eqApplyPreset(std::string preset_name) { return {{EqApplyPresetPayload{std::move(preset_name)}}}; }
    [[nodiscard]] static RuntimeCommandPayload audioEffectCycle(std::string plugin_id = {}, int delta = 1)
    {
        return {{AudioEffectCyclePayload{std::move(plugin_id), delta}}};
    }
    [[nodiscard]] static RuntimeCommandPayload settingsApplyLive(std::string output_mode, std::string network_policy, std::string sleep_timer)
    {
        return {{SettingsApplyLivePayload{std::move(output_mode), std::move(network_policy), std::move(sleep_timer)}}};
    }
    [[nodiscard]] static RuntimeCommandPayload remoteResolvedStream(app::ResolvedRemoteStream stream, app::RemoteTrack track, std::string source, RemoteSessionSnapshot snapshot)
    {
        return {{RemotePlayResolvedStreamPayload{std::move(stream), std::move(track), std::move(source), std::move(snapshot)}}};
    }
    [[nodiscard]] static RuntimeCommandPayload remoteResolvedLibraryTrack(
        int local_track_id,
        app::RemoteServerProfile profile,
        app::RemoteTrack track,
        app::ResolvedRemoteStream stream,
        std::string source,
        bool cache_remote_facts,
        RemoteSessionSnapshot snapshot)
    {
        return {{RemotePlayResolvedLibraryTrackPayload{
            local_track_id,
            std::move(profile),
            std::move(track),
            std::move(stream),
            std::move(source),
            cache_remote_facts,
            std::move(snapshot)}}};
    }

    template <typename Payload>
    [[nodiscard]] const Payload* get() const noexcept
    {
        return std::get_if<Payload>(&data);
    }
};

struct RuntimeCommand {
    RuntimeCommandKind kind{RuntimeCommandKind::PlaybackToggle};
    RuntimeCommandPayload payload{};
    CommandOrigin origin{CommandOrigin::Unknown};
    std::string correlation_id{};
};

struct RuntimeQuery {
    RuntimeQueryKind kind{RuntimeQueryKind::FullSnapshot};
    CommandOrigin origin{CommandOrigin::Unknown};
    std::string correlation_id{};
};

} // namespace lofibox::runtime
