// SPDX-License-Identifier: GPL-3.0-or-later

#include "tui/pages/library_tui_page.h"

#include "tui/widgets/tui_text.h"

namespace lofibox::tui::pages {

std::vector<std::string> libraryPageLines(const TuiModel& model, const TuiLayout& layout)
{
    const auto& library = model.snapshot.library;
    std::vector<std::string> lines{
        widgets::fitText("Library - " + library.status, layout.size.width),
        widgets::fitText("tracks: " + std::to_string(library.track_count), layout.size.width),
        widgets::fitText("albums: " + std::to_string(library.album_count), layout.size.width),
        widgets::fitText("artists: " + std::to_string(library.artist_count), layout.size.width),
    };

    if (library.status == "LOADING") {
        lines.push_back(widgets::fitText("scan: " + library.scan_phase, layout.size.width));
        if (library.scan_files_total > 0) {
            lines.push_back(widgets::fitText(
                "files: " + std::to_string(library.scan_files_processed) + "/" + std::to_string(library.scan_files_total),
                layout.size.width));
        } else {
            lines.push_back(widgets::fitText(
                "files: " + std::to_string(library.scan_files_discovered) + " discovered",
                layout.size.width));
        }
        lines.push_back(widgets::fitText("indexed: " + std::to_string(library.scan_tracks_indexed), layout.size.width));
        if (!library.scan_current_path.empty()) {
            lines.push_back(widgets::fitText("path: " + library.scan_current_path, layout.size.width));
        }
    }

    lines.push_back(widgets::fitText(library.degraded ? "status: degraded" : "status: ok", layout.size.width));
    return lines;
}

} // namespace lofibox::tui::pages
