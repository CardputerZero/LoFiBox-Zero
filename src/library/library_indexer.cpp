// SPDX-License-Identifier: GPL-3.0-or-later

#include "library/library_indexer.h"

#include "app/library_scanner.h"

#include <utility>

namespace lofibox::library {

app::LibraryModel LibraryIndexer::rebuild(
    const std::vector<std::filesystem::path>& media_roots,
    const app::MetadataProvider& metadata_provider,
    app::LibraryScanProgressCallback progress) const
{
    return app::scanLibrary(media_roots, metadata_provider, std::move(progress));
}

} // namespace lofibox::library
