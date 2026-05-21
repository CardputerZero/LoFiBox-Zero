// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <optional>

#include "app/library_model.h"
#include "playback/playback_state.h"

namespace lofibox::app {

class PlaybackSessionClock {
public:
    void resetForTrack(PlaybackSession& session) const noexcept;
    void advance(PlaybackSession& session, double delta_seconds) const noexcept;
    void advance(PlaybackSession& session, double delta_seconds, std::optional<double> backend_position_seconds) const noexcept;
    void clampToTrackDuration(PlaybackSession& session, const TrackRecord* track) const noexcept;
};

} // namespace lofibox::app

