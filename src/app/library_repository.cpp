// SPDX-License-Identifier: GPL-3.0-or-later

#include "app/library_repository.h"

#include "library/library_governance.h"
#include "library/library_indexer.h"

#include <utility>

namespace lofibox::app {

LibraryIndexState LibraryRepository::state() const noexcept
{
    return state_;
}

const LibraryModel& LibraryRepository::model() const noexcept
{
    return library_;
}

LibraryModel& LibraryRepository::mutableModel() noexcept
{
    return library_;
}

const std::vector<::lofibox::library::LibraryFileChange>& LibraryRepository::lastChanges() const noexcept
{
    return last_changes_;
}

const std::vector<::lofibox::library::LibraryMigration>& LibraryRepository::migrationPlan() const noexcept
{
    return migration_plan_;
}

void LibraryRepository::markLoading() noexcept
{
    state_ = LibraryIndexState::Loading;
}

void LibraryRepository::markDegraded() noexcept
{
    library_.degraded = true;
    state_ = LibraryIndexState::Degraded;
}

void LibraryRepository::rescan(
    const std::vector<std::filesystem::path>& media_roots,
    const MetadataProvider& metadata_provider,
    LibraryScanProgressCallback progress)
{
    applyRescanModel(rebuildModel(media_roots, metadata_provider, std::move(progress)));
}

LibraryModel LibraryRepository::rebuildModel(
    const std::vector<std::filesystem::path>& media_roots,
    const MetadataProvider& metadata_provider,
    LibraryScanProgressCallback progress) const
{
    return library::LibraryIndexer{}.rebuild(media_roots, metadata_provider, std::move(progress));
}

void LibraryRepository::applyRescanModel(LibraryModel next)
{
    const auto before = library_;
    std::vector<std::filesystem::path> current_files{};
    current_files.reserve(next.tracks.size());
    for (const auto& track : next.tracks) {
        current_files.push_back(track.path);
    }
    library::LibraryGovernanceService governance{};
    last_changes_ = governance.incrementalChanges(before, current_files);
    migration_plan_ = governance.migrationPlan(1, 1);
    library_ = std::move(next);
    state_ = library_.degraded ? LibraryIndexState::Degraded : LibraryIndexState::Ready;
}

void LibraryRepository::rebuildDerivedIndexes()
{
    rebuildLibraryIndexes(library_);
}

const TrackRecord* LibraryRepository::findTrack(int id) const noexcept
{
    for (const auto& track : library_.tracks) {
        if (track.id == id) {
            return &track;
        }
    }
    return nullptr;
}

TrackRecord* LibraryRepository::findMutableTrack(int id) noexcept
{
    for (auto& track : library_.tracks) {
        if (track.id == id) {
            return &track;
        }
    }
    return nullptr;
}

} // namespace lofibox::app
