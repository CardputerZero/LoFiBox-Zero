// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <filesystem>
#include <vector>

#include "app/library_model.h"
#include "app/library_scanner.h"
#include "app/runtime_services.h"
#include "library/library_governance.h"

namespace lofibox::app {

class LibraryRepository {
public:
    [[nodiscard]] LibraryIndexState state() const noexcept;
    [[nodiscard]] const LibraryModel& model() const noexcept;
    [[nodiscard]] LibraryModel& mutableModel() noexcept;
    [[nodiscard]] const std::vector<::lofibox::library::LibraryFileChange>& lastChanges() const noexcept;
    [[nodiscard]] const std::vector<::lofibox::library::LibraryMigration>& migrationPlan() const noexcept;

    void markLoading() noexcept;
    void markDegraded() noexcept;
    void rescan(
        const std::vector<std::filesystem::path>& media_roots,
        const MetadataProvider& metadata_provider,
        LibraryScanProgressCallback progress = {});
    void applyRescanModel(LibraryModel next);

    [[nodiscard]] const TrackRecord* findTrack(int id) const noexcept;
    [[nodiscard]] TrackRecord* findMutableTrack(int id) noexcept;

private:
    LibraryIndexState state_{LibraryIndexState::Uninitialized};
    LibraryModel library_{};
    std::vector<::lofibox::library::LibraryFileChange> last_changes_{};
    std::vector<::lofibox::library::LibraryMigration> migration_plan_{};
};

} // namespace lofibox::app
