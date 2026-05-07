// SPDX-License-Identifier: GPL-3.0-or-later

#include "runtime/runtime_command_dispatcher.h"

#include <iostream>
#include <string>

namespace lofibox::runtime {

RuntimeCommandDispatcher::RuntimeCommandDispatcher(RuntimeSessionFacade& session) noexcept
    : session_(session)
{
}

RuntimeCommandResult RuntimeCommandDispatcher::dispatch(const RuntimeCommand& command)
{
    switch (command.kind) {
    case RuntimeCommandKind::PlaybackPlay:
        return applied(command, "PLAYBACK_PLAY", "Playback play submitted.", session_.playFirstAvailable());
    case RuntimeCommandKind::PlaybackPause:
        session_.playback().pause();
        return applied(command, "PLAYBACK_PAUSE", "Playback pause submitted.", true);
    case RuntimeCommandKind::PlaybackResume:
        return applied(command, "PLAYBACK_RESUME", "Playback resume submitted.", session_.resumePlayback());
    case RuntimeCommandKind::PlaybackToggle:
        return applied(command, "PLAYBACK_TOGGLE", "Playback toggle submitted.", session_.togglePlayPause());
    case RuntimeCommandKind::PlaybackStartTrack:
        {
            const auto* payload = command.payload.get<PlaybackStartTrackPayload>();
            if (payload == nullptr) {
                return applied(command, "PLAYBACK_START_TRACK", "Playback start-track submitted.", false);
            }
            std::cerr << "[webui] dispatcher PlaybackStartTrack: track_id=" << payload->track_id
                      << " album=\"" << payload->album << "\""
                      << " artist=\"" << payload->artist << "\""
                      << " genre=\"" << payload->genre << "\"\n";
            const bool ok = (!payload->album.empty() || !payload->genre.empty())
                ? session_.startTrackWithContext(payload->track_id, *payload)
                : session_.startTrack(payload->track_id);
            return applied(command, "PLAYBACK_START_TRACK", "Playback start-track submitted.", ok);
        }
    case RuntimeCommandKind::PlaybackStop:
        session_.playback().stop();
        return applied(command, "PLAYBACK_STOP", "Playback stop submitted.", true);
    case RuntimeCommandKind::PlaybackSeek:
        {
            const auto* payload = command.payload.get<PlaybackSeekPayload>();
            return applied(command, "PLAYBACK_SEEK", "Playback seek submitted.", payload != nullptr && session_.playback().seek(payload->seconds));
        }
    case RuntimeCommandKind::QueueStep:
        {
            const auto* payload = command.payload.get<QueueStepPayload>();
            return applied(command, "QUEUE_STEP", "Queue step submitted.", payload != nullptr && session_.stepQueue(payload->delta));
        }
    case RuntimeCommandKind::QueueJump:
        {
            const auto* payload = command.payload.get<QueueIndexPayload>();
            return applied(command, "QUEUE_JUMP", "Queue jump submitted.", payload != nullptr && session_.jumpQueue(payload->queue_index));
        }
    case RuntimeCommandKind::QueueClear:
        session_.queue().clear();
        return applied(command, "QUEUE_CLEAR", "Queue clear submitted.", true);
    case RuntimeCommandKind::PlaybackToggleShuffle:
        session_.queue().toggleShuffle();
        return applied(command, "PLAYBACK_SHUFFLE", "Playback shuffle submitted.", true);
    case RuntimeCommandKind::PlaybackSetShuffle:
        {
            const auto* payload = command.payload.get<RuntimeEnabledPayload>();
            if (payload == nullptr) {
                return applied(command, "PLAYBACK_SHUFFLE", "Shuffle payload missing.", false);
            }
            session_.queue().setShuffleEnabled(payload->enabled);
            return applied(command, "PLAYBACK_SHUFFLE", "Playback shuffle submitted.", true);
        }
    case RuntimeCommandKind::PlaybackCycleRepeat:
        session_.queue().cycleRepeatMode();
        return applied(command, "PLAYBACK_REPEAT", "Playback repeat mode submitted.", true);
    case RuntimeCommandKind::PlaybackCycleMainMenuMode:
        session_.queue().cycleMainMenuPlaybackMode();
        return applied(command, "PLAYBACK_MENU_MODE", "Main-menu playback mode submitted.", true);
    case RuntimeCommandKind::PlaybackSetRepeatAll:
        {
            const auto* payload = command.payload.get<RuntimeEnabledPayload>();
            if (payload == nullptr) {
                return applied(command, "PLAYBACK_REPEAT_ALL", "Repeat-all payload missing.", false);
            }
            session_.queue().setRepeatAll(payload->enabled);
            return applied(command, "PLAYBACK_REPEAT_ALL", "Repeat-all submitted.", true);
        }
    case RuntimeCommandKind::PlaybackSetRepeatOne:
        {
            const auto* payload = command.payload.get<RuntimeEnabledPayload>();
            if (payload == nullptr) {
                return applied(command, "PLAYBACK_REPEAT_ONE", "Repeat-one payload missing.", false);
            }
            session_.queue().setRepeatOne(payload->enabled);
            return applied(command, "PLAYBACK_REPEAT_ONE", "Repeat-one submitted.", true);
        }
    case RuntimeCommandKind::RemoteResolveAndStartTrack:
        {
            const auto* payload = command.payload.get<RemoteTrackRefPayload>();
            return applied(
                command,
                "REMOTE_RESOLVE_AND_START_TRACK",
                "Remote item resolve/start submitted.",
                payload != nullptr && session_.startRemoteItem(payload->profile_id, payload->item_id));
        }
    case RuntimeCommandKind::RemoteStartActiveStream:
        {
            if (const auto* library_payload = command.payload.get<RemotePlayResolvedLibraryTrackPayload>()) {
                const bool started = session_.playback().startRemoteLibraryTrack(*library_payload);
                if (started) {
                    session_.remote().setSnapshot(library_payload->snapshot);
                }
                return applied(command, "REMOTE_START_ACTIVE_STREAM", "Remote library stream playback submitted.", started);
            }
            if (const auto* stream_payload = command.payload.get<RemotePlayResolvedStreamPayload>()) {
                const bool started = session_.playback().startRemoteStream(*stream_payload);
                if (started) {
                    session_.remote().setSnapshot(stream_payload->snapshot);
                }
                return applied(command, "REMOTE_START_ACTIVE_STREAM", "Remote stream playback submitted.", started);
            }
            return applied(command, "REMOTE_START_ACTIVE_STREAM", "Resolved remote stream payload missing.", false);
        }
    case RuntimeCommandKind::RemoteReconnect:
        return applied(command, "REMOTE_RECONNECT", "Remote reconnect submitted.", session_.remote().reconnect());
    case RuntimeCommandKind::EqEnable:
        session_.eq().setEnabled(true);
        return applied(command, "EQ_ENABLE", "EQ enable submitted.", true);
    case RuntimeCommandKind::EqDisable:
        session_.eq().setEnabled(false);
        return applied(command, "EQ_DISABLE", "EQ disable submitted.", true);
    case RuntimeCommandKind::EqSetBand:
        {
            const auto* payload = command.payload.get<EqSetBandPayload>();
            return applied(command, "EQ_SET_BAND", "EQ band set submitted.", payload != nullptr && session_.eq().setBand(payload->eq_band_index, payload->eq_gain_db));
        }
    case RuntimeCommandKind::EqAdjustBand:
        {
            const auto* payload = command.payload.get<EqAdjustBandPayload>();
            return applied(command, "EQ_ADJUST_BAND", "EQ band adjustment submitted.", payload != nullptr && session_.eq().adjustBand(payload->eq_band_index, payload->eq_gain_delta));
        }
    case RuntimeCommandKind::EqApplyPreset:
        {
            const auto* payload = command.payload.get<EqApplyPresetPayload>();
            return applied(command, "EQ_APPLY_PRESET", "EQ preset submitted.", payload != nullptr && session_.eq().applyPreset(payload->preset_name));
        }
    case RuntimeCommandKind::EqCyclePreset:
        {
            const auto* payload = command.payload.get<EqCyclePresetPayload>();
            return applied(command, "EQ_CYCLE_PRESET", "EQ preset cycle submitted.", payload != nullptr && session_.eq().cyclePreset(payload->preset_delta));
        }
    case RuntimeCommandKind::EqReset:
        session_.eq().reset();
        return applied(command, "EQ_RESET", "EQ reset submitted.", true);
    case RuntimeCommandKind::AudioEffectCycle:
        {
            const auto* payload = command.payload.get<AudioEffectCyclePayload>();
            const auto plugin_id = payload == nullptr ? std::string{} : payload->plugin_id;
            const int delta = payload == nullptr ? 1 : payload->delta;
            return applied(command, "AUDIO_EFFECT_CYCLE", "Audio effect cycle submitted.", session_.eq().cycleAudioEffect(plugin_id, delta));
        }
    case RuntimeCommandKind::SettingsApplyLive:
        {
            const auto* payload = command.payload.get<SettingsApplyLivePayload>();
            if (payload == nullptr) {
                return applied(command, "SETTINGS_APPLY_LIVE", "Settings payload missing.", false);
            }
            session_.settings().applyLive(payload->output_mode, payload->network_policy, payload->sleep_timer);
            return applied(command, "SETTINGS_APPLY_LIVE", "Live settings submitted.", true);
        }
    case RuntimeCommandKind::RuntimeShutdown:
        session_.settings().requestShutdown();
        return applied(command, "RUNTIME_SHUTDOWN", "Runtime shutdown requested.", true);
    case RuntimeCommandKind::RuntimeReload:
        session_.settings().requestReload();
        return applied(command, "RUNTIME_RELOAD", "Runtime reload requested.", true);
    }
    return rejectUnsupported(command, "Unknown runtime command.");
}

std::uint64_t RuntimeCommandDispatcher::version() const noexcept
{
    return version_;
}

RuntimeCommandResult RuntimeCommandDispatcher::rejectUnsupported(const RuntimeCommand& command, const char* message) const
{
    return RuntimeCommandResult::reject("UNSUPPORTED_RUNTIME_COMMAND", message, command.origin, command.correlation_id, version_, version_);
}

RuntimeCommandResult RuntimeCommandDispatcher::applied(const RuntimeCommand& command, const char* code, const char* message, bool applied)
{
    const auto version_before_apply = version_;
    if (applied) {
        ++version_;
    }
    return RuntimeCommandResult::ok(code, message, command.origin, command.correlation_id, version_before_apply, version_, applied);
}

} // namespace lofibox::runtime
