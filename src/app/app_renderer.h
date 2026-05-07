// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <chrono>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "app/app_page.h"
#include "app/app_page_model.h"
#include "app/app_state.h"
#include "app/library_model.h"
#include "app/navigation_state.h"
#include "playback/playback_state.h"
#include "core/canvas.h"
#include "runtime/runtime_snapshot.h"
#include "ui/ui_models.h"
#include "ui/ui_theme.h"

namespace lofibox::app {

class AppRenderTarget {
public:
    virtual ~AppRenderTarget() = default;

    [[nodiscard]] virtual AppPage currentPage() const noexcept = 0;
    [[nodiscard]] virtual const ui::UiAssets& assets() const noexcept = 0;
    [[nodiscard]] virtual std::chrono::steady_clock::time_point bootStarted() const noexcept = 0;
    [[nodiscard]] virtual LibraryIndexState libraryState() const noexcept = 0;
    [[nodiscard]] virtual LibraryScanProgress libraryScanProgress() const { return {}; }
    [[nodiscard]] virtual StorageInfo storage() const = 0;
    [[nodiscard]] virtual bool networkConnected() const noexcept = 0;
    [[nodiscard]] virtual int mainMenuIndex() const noexcept = 0;
    [[nodiscard]] virtual const PlaybackSession& playbackSession() const noexcept = 0;
    [[nodiscard]] virtual const TrackRecord* findTrack(int id) const noexcept = 0;
    [[nodiscard]] virtual const EqState& eqState() const noexcept = 0;
    [[nodiscard]] virtual runtime::EqRuntimeSnapshot eqRuntimeSnapshot() const noexcept = 0;
    [[nodiscard]] virtual ListSelection listSelection() const noexcept = 0;
    [[nodiscard]] virtual AppPageModel pageModel() const = 0;
    [[nodiscard]] virtual bool helpOpen() const noexcept = 0;
    [[nodiscard]] virtual AppPage helpPage() const noexcept = 0;
    [[nodiscard]] virtual const ui::UiTheme& theme() const noexcept = 0;
};

void renderApp(core::Canvas& canvas, const AppRenderTarget& target);

} // namespace lofibox::app
