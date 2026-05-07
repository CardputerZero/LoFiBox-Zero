// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <filesystem>
#include <vector>

#include "app/library_model.h"
#include "app/library_scanner.h"
#include "app/runtime_services.h"

namespace lofibox::library {

class LibraryIndexer {
public:
    [[nodiscard]] app::LibraryModel rebuild(
        const std::vector<std::filesystem::path>& media_roots,
        const app::MetadataProvider& metadata_provider,
        app::LibraryScanProgressCallback progress = {}) const;
};

} // namespace lofibox::library
