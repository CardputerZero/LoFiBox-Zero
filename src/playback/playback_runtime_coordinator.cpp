// SPDX-License-Identifier: GPL-3.0-or-later

#include "playback/playback_runtime_coordinator.h"

#include <algorithm>
#include <cmath>
#include <optional>
#include <utility>

namespace lofibox::app {
namespace {

constexpr double kAcceptFinishRemainingSeconds = 1.5;

void resetFinishPending(PlaybackSession& session) noexcept
{
    session.finish_pending = false;
    session.finish_pending_seconds = 0.0;
}

bool deferEarlyFinish(
    PlaybackSession& session,
    int duration_seconds,
    double delta_seconds,
    std::optional<double> backend_position_seconds) noexcept
{
    if (duration_seconds <= 0) {
        return false;
    }

    const double duration = static_cast<double>(duration_seconds);
    double observed_elapsed = session.elapsed_seconds;
    if (backend_position_seconds && std::isfinite(*backend_position_seconds)) {
        observed_elapsed = std::max(0.0, *backend_position_seconds);
    }
    observed_elapsed = std::min(observed_elapsed, duration);

    if (duration - observed_elapsed <= kAcceptFinishRemainingSeconds) {
        session.elapsed_seconds = observed_elapsed;
        return false;
    }

    session.finish_pending = true;
    session.finish_pending_seconds += std::max(0.0, delta_seconds);
    if (backend_position_seconds && std::isfinite(*backend_position_seconds)) {
        session.elapsed_seconds = observed_elapsed;
    } else if (session.status == PlaybackStatus::Playing) {
        session.elapsed_seconds = std::min(duration, observed_elapsed + std::max(0.0, delta_seconds));
    }
    session.visualization_frame = {};
    return true;
}

} // namespace

void PlaybackRuntimeCoordinator::setServices(RuntimeServices* services) noexcept
{
    audio_pipeline_.bind(services);
}

void PlaybackRuntimeCoordinator::setDspProfile(audio::dsp::DspChainProfile profile)
{
    audio_pipeline_.setDspProfile(std::move(profile));
}

void PlaybackRuntimeCoordinator::beginTrack(PlaybackSession& session) const noexcept
{
    clock_.resetForTrack(session);
    resetFinishPending(session);
    session.current_lyrics = {};
    session.lyrics_lookup_pending = true;
    session.visualization_pending = false;
    session.visualization_frame = {};
}

bool PlaybackRuntimeCoordinator::startBackend(const std::filesystem::path& path, PlaybackSession& session)
{
    session.audio_active = audio_pipeline_.startFile(path, 0.0);
    return session.audio_active;
}

bool PlaybackRuntimeCoordinator::startBackendUri(const std::string& uri, PlaybackSession& session)
{
    session.audio_active = audio_pipeline_.startUri(uri, 0.0);
    return session.audio_active;
}

bool PlaybackRuntimeCoordinator::seekBackend(const std::filesystem::path& path, double seconds, PlaybackSession& session)
{
    session.elapsed_seconds = std::max(0.0, seconds);
    resetFinishPending(session);
    session.audio_active = audio_pipeline_.startFile(path, session.elapsed_seconds);
    if (session.audio_active) {
        session.status = PlaybackStatus::Playing;
        return true;
    }
    session.status = PlaybackStatus::Paused;
    session.visualization_pending = false;
    session.visualization_frame = {};
    return false;
}

void PlaybackRuntimeCoordinator::pause(PlaybackSession& session) noexcept
{
    if (session.status == PlaybackStatus::Playing) {
        audio_pipeline_.pause();
        session.status = PlaybackStatus::Paused;
    }
}

void PlaybackRuntimeCoordinator::resume(PlaybackSession& session) noexcept
{
    if (session.status != PlaybackStatus::Paused) {
        return;
    }
    if (!session.audio_active) {
        session.visualization_pending = false;
        session.visualization_frame = {};
        return;
    }

    audio_pipeline_.resume();
    switch (audio_pipeline_.state()) {
    case AudioPlaybackState::Starting:
    case AudioPlaybackState::Playing:
        session.status = PlaybackStatus::Playing;
        break;
    case AudioPlaybackState::Paused:
        session.status = PlaybackStatus::Paused;
        break;
    case AudioPlaybackState::Idle:
    case AudioPlaybackState::Finished:
    case AudioPlaybackState::Failed:
        session.status = PlaybackStatus::Paused;
        session.audio_active = false;
        session.visualization_pending = false;
        session.visualization_frame = {};
        break;
    }
}

void PlaybackRuntimeCoordinator::stop(PlaybackSession& session) noexcept
{
    audio_pipeline_.stop();
    session.status = PlaybackStatus::Paused;
    session.audio_active = false;
    session.elapsed_seconds = 0.0;
    resetFinishPending(session);
    session.visualization_pending = false;
    session.visualization_frame = {};
}

bool PlaybackRuntimeCoordinator::stepQueue(const QueueState& queue, const PlaybackSession& session, int delta, const PlayIndexCallback& play_index) const
{
    const int index = completion_policy_.steppedIndex(queue, session, delta);
    if (index >= 0) {
        return play_index(index);
    }
    return false;
}

bool PlaybackRuntimeCoordinator::advanceAfterFinish(const QueueState& queue, const PlaybackSession& session, const PlayIndexCallback& play_index) const
{
    const auto index = completion_policy_.nextIndexAfterFinish(queue, session);
    return index.has_value() && play_index(*index);
}

void PlaybackRuntimeCoordinator::tick(
    PlaybackSession& session,
    const QueueState& queue,
    const LibraryController& library_controller,
    double delta_seconds,
    const PlayIndexCallback& play_index)
{
    const auto* current_track = session.current_track_id ? library_controller.findTrack(*session.current_track_id) : nullptr;
    const int stream_duration_seconds = session.current_stream_duration_seconds > 0 ? session.current_stream_duration_seconds : 0;
    const int duration_seconds = current_track ? current_track->duration_seconds : stream_duration_seconds;
    const auto duration = std::chrono::milliseconds{std::max(0, duration_seconds) * 1000};
    const auto backend_state = session.audio_active ? audio_pipeline_.state() : AudioPlaybackState::Idle;
    const auto backend_position = session.audio_active ? audio_pipeline_.positionSeconds() : std::optional<double>{};

    if (session.audio_active && backend_state == AudioPlaybackState::Finished) {
        if (deferEarlyFinish(session, duration_seconds, delta_seconds, backend_position)) {
            return;
        }
        resetFinishPending(session);
        if (advanceAfterFinish(queue, session, play_index)) {
            return;
        }
        session.audio_active = false;
        session.status = PlaybackStatus::Paused;
        if (current_track) {
            clock_.clampToTrackDuration(session, current_track);
        } else if (stream_duration_seconds > 0) {
            session.elapsed_seconds = std::min(session.elapsed_seconds, static_cast<double>(stream_duration_seconds));
        }
        session.visualization_frame = {};
        return;
    }

    if (session.audio_active) {
        switch (backend_state) {
        case AudioPlaybackState::Finished:
            break;
        case AudioPlaybackState::Failed:
            session.audio_active = false;
            session.status = PlaybackStatus::Paused;
            resetFinishPending(session);
            session.visualization_frame = {};
            return;
        case AudioPlaybackState::Idle:
        case AudioPlaybackState::Starting:
        case AudioPlaybackState::Playing:
        case AudioPlaybackState::Paused:
            break;
        }
    }

    const bool backend_progressing = session.audio_active && backend_state == AudioPlaybackState::Playing;
    if (backend_progressing) {
        resetFinishPending(session);
        clock_.advance(session, delta_seconds, backend_position);
    }
    const PlaybackStabilitySample stability_sample{
        std::chrono::milliseconds{static_cast<int>(session.elapsed_seconds * 1000.0)},
        duration,
        backend_progressing && session.elapsed_seconds < 0.25,
        false,
        false};
    if (stability_policy_.shouldPrepareNext(stability_sample, transition_policy_)) {
        session.visualization_pending = true;
    }
    session.visualization_frame = backend_progressing ? audio_pipeline_.visualizationFrame() : AudioVisualizationFrame{};
}

} // namespace lofibox::app

