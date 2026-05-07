// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <filesystem>
#include <functional>
#include <vector>

#include "app/library_model.h"
#include "app/runtime_services.h"

namespace lofibox::app {

using LibraryScanProgressCallback = std::function<void(const LibraryScanProgress&)>;

[[nodiscard]] LibraryModel scanLibrary(
    const std::vector<std::filesystem::path>& requested_roots,
    const MetadataProvider& metadata_provider,
    LibraryScanProgressCallback progress = {});
void rebuildLibraryIndexes(LibraryModel& model);

} // namespace lofibox::app
