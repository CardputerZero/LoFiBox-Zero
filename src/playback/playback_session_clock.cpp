// SPDX-License-Identifier: GPL-3.0-or-later

#include "playback/playback_session_clock.h"

#include <algorithm>
#include <cmath>

namespace lofibox::app {

void PlaybackSessionClock::resetForTrack(PlaybackSession& session) const noexcept
{
    session.elapsed_seconds = 0.0;
    ++session.transition_generation;
}

void PlaybackSessionClock::advance(PlaybackSession& session, double delta_seconds) const noexcept
{
    if (session.status == PlaybackStatus::Playing) {
        session.elapsed_seconds += std::max(0.0, delta_seconds);
    }
}

void PlaybackSessionClock::advance(
    PlaybackSession& session,
    double delta_seconds,
    std::optional<double> backend_position_seconds) const noexcept
{
    if (session.status != PlaybackStatus::Playing) {
        return;
    }
    if (backend_position_seconds && std::isfinite(*backend_position_seconds)) {
        session.elapsed_seconds = std::max(0.0, *backend_position_seconds);
        return;
    }
    advance(session, delta_seconds);
}

void PlaybackSessionClock::clampToTrackDuration(PlaybackSession& session, const TrackRecord* track) const noexcept
{
    if (track == nullptr) {
        return;
    }
    session.elapsed_seconds = std::min(session.elapsed_seconds, static_cast<double>(std::max(0, track->duration_seconds)));
}

} // namespace lofibox::app

