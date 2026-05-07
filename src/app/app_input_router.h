// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <string_view>

#include "app/app_page.h"
#include "app/input_event.h"

namespace lofibox::app {

class AppInputTarget {
public:
    virtual ~AppInputTarget() = default;

    [[nodiscard]] virtual AppPage currentPage() const noexcept = 0;
    [[nodiscard]] virtual bool helpOpen() const noexcept = 0;
    [[nodiscard]] virtual bool isBrowseListPage() const noexcept = 0;
    [[nodiscard]] virtual bool nowPlayingConfirmBlocked() const = 0;

    virtual void toggleHelpForCurrentPage() = 0;
    virtual void closeHelp() = 0;
    virtual void pushPage(AppPage page) = 0;
    virtual void popPage() = 0;

    virtual void playFromMenu() = 0;
    virtual void pausePlayback() = 0;
    virtual void stepTrack(int delta) = 0;
    virtual void cycleMainMenuPlaybackMode() = 0;
    virtual void toggleRepeatAll() = 0;
    virtual void toggleRepeatOne() = 0;
    virtual void openSearchPage() = 0;
    virtual void openLibraryPage() = 0;
    virtual void openQueuePage() = 0;
    virtual void openSettingsPage() = 0;
    virtual void showMainMenuPage() = 0;
    virtual void moveMainMenuSelection(int delta) = 0;
    virtual void resetMainMenuSelection() = 0;
    virtual void confirmMainMenu() = 0;

    virtual void toggleShuffle() = 0;
    virtual void cycleRepeatMode() = 0;
    virtual void togglePlayPause() = 0;
    virtual void cycleAudioEffect() = 0;

    virtual void moveEqualizerSelection(int delta) = 0;
    virtual void adjustSelectedEqualizerBand(int delta) = 0;
    virtual void cycleEqualizerPreset(int delta) = 0;

    virtual void cycleSongSortModeAndClamp() = 0;
    virtual void moveSelection(int delta) = 0;
    virtual void moveSelectionPage(int delta_pages) = 0;
    virtual void confirmListPage() = 0;
    virtual void appendSearchText(std::string_view text) { (void)text; }
    virtual void backspaceSearchQuery() {}
    virtual void appendRemoteProfileEditText(std::string_view text) { (void)text; }
    virtual void backspaceRemoteProfileEdit() {}
    virtual void commitRemoteProfileEdit() {}
};

void routeInput(AppInputTarget& target, const InputEvent& event);

} // namespace lofibox::app
