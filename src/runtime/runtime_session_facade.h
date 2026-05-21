// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <string>

#include "application/app_service_registry.h"
#include "runtime/creator_analysis_runtime.h"
#include "runtime/eq_runtime_state.h"
#include "runtime/eq_runtime.h"
#include "runtime/playback_runtime.h"
#include "runtime/queue_runtime.h"
#include "runtime/remote_session_runtime.h"
#include "runtime/runtime_snapshot_assembler.h"
#include "runtime/settings_runtime_state.h"
#include "runtime/settings_runtime.h"
#include "runtime/runtime_snapshot.h"

namespace lofibox::runtime {

class RuntimeSessionFacade {
public:
    RuntimeSessionFacade(application::AppServiceRegistry services, EqRuntimeState& eq, SettingsRuntimeState& settings) noexcept;

    [[nodiscard]] PlaybackRuntime& playback() noexcept;
    [[nodiscard]] const PlaybackRuntime& playback() const noexcept;
    [[nodiscard]] QueueRuntime& queue() noexcept;
    [[nodiscard]] const QueueRuntime& queue() const noexcept;
    [[nodiscard]] EqRuntime& eq() noexcept;
    [[nodiscard]] const EqRuntime& eq() const noexcept;
    [[nodiscard]] RemoteSessionRuntime& remote() noexcept;
    [[nodiscard]] const RemoteSessionRuntime& remote() const noexcept;
    [[nodiscard]] SettingsRuntime& settings() noexcept;
    [[nodiscard]] const SettingsRuntime& settings() const noexcept;
    [[nodiscard]] bool playFirstAvailable();
    [[nodiscard]] bool resumePlayback();
    [[nodiscard]] bool togglePlayPause();
    [[nodiscard]] bool startTrack(int track_id);
    [[nodiscard]] bool startTrackWithContext(int track_id, const PlaybackStartTrackPayload& context);
    [[nodiscard]] bool startRemoteItem(const std::string& profile_id, const std::string& item_id);
    [[nodiscard]] bool stepQueue(int delta);
    [[nodiscard]] bool jumpQueue(int queue_index);
    [[nodiscard]] bool refreshLibrary();
    void tick(double delta_seconds);
    [[nodiscard]] RuntimeSnapshot snapshot(std::uint64_t version) const;

private:
    application::AppServiceRegistry services_;
    PlaybackRuntime playback_;
    QueueRuntime queue_;
    EqRuntime eq_;
    RemoteSessionRuntime remote_;
    SettingsRuntime settings_;
    CreatorAnalysisRuntime creator_;
    RuntimeSnapshotAssembler snapshots_{};
};

} // namespace lofibox::runtime
