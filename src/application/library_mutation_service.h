// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "app/library_model.h"
#include "app/remote_media_services.h"
#include "app/runtime_services.h"

namespace lofibox::app {
class LibraryController;
}

namespace lofibox::application {

class LibraryMutationService {
public:
    explicit LibraryMutationService(app::LibraryController& controller) noexcept;
    LibraryMutationService(app::LibraryController& controller, const app::RuntimeServices& services) noexcept;

    void startLoading() const noexcept;
    void refreshLibrary(const std::vector<std::filesystem::path>& media_roots, const app::MetadataProvider& metadata_provider) const;
    [[nodiscard]] bool refreshLibrary(const std::vector<std::filesystem::path>& media_roots) const;
    [[nodiscard]] bool beginAsyncRefreshLibrary(const std::vector<std::filesystem::path>& media_roots) const;
    [[nodiscard]] bool pollAsyncRefreshLibrary() const;
    [[nodiscard]] app::LibraryScanProgress scanProgress() const;
    void mergeRemoteTracks(const app::RemoteServerProfile& profile, const std::vector<app::RemoteTrack>& tracks) const;
    [[nodiscard]] bool applyRemoteTrackFacts(const app::RemoteServerProfile& profile, const app::RemoteTrack& remote_track) const;
    void setSongsContextAll() const;
    void setSongsContextTrackIds(std::string label, std::vector<int> ids) const;
    void cycleSongSortMode() const;
    void recordPlaybackStarted(app::TrackRecord& track) const;

private:
    app::LibraryController& controller_;
    const app::RuntimeServices* services_{nullptr};
};

} // namespace lofibox::application
