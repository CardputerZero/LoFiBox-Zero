// SPDX-License-Identifier: GPL-3.0-or-later

#include "app/app_input_router.h"

#include <cctype>

#include "app/input_actions.h"

namespace lofibox::app {
namespace {

[[nodiscard]] bool isLyricsToggle(const InputEvent& event) noexcept
{
    const auto ch = singleAsciiText(event);
    return ch && std::toupper(static_cast<unsigned char>(*ch)) == 'L';
}

[[nodiscard]] bool isQueueToggle(const InputEvent& event) noexcept
{
    const auto ch = singleAsciiText(event);
    return ch && std::toupper(static_cast<unsigned char>(*ch)) == 'Q';
}

[[nodiscard]] bool isSortShortcut(const InputEvent& event) noexcept
{
    const auto ch = singleAsciiText(event);
    return ch && std::toupper(static_cast<unsigned char>(*ch)) == 'T';
}

[[nodiscard]] bool isPlaylistEditShortcut(const InputEvent& event) noexcept
{
    const auto ch = singleAsciiText(event);
    return ch && std::toupper(static_cast<unsigned char>(*ch)) == 'E';
}

[[nodiscard]] bool isAudioEffectShortcut(const InputEvent& event) noexcept
{
    const auto ch = singleAsciiText(event);
    return ch && std::toupper(static_cast<unsigned char>(*ch)) == 'R';
}

[[nodiscard]] bool routeGlobalTransportShortcut(AppInputTarget& target, const InputEvent& event)
{
    switch (event.key) {
    case InputKey::F2:
        target.playFromMenu();
        return true;
    case InputKey::F3:
        target.pausePlayback();
        return true;
    case InputKey::F4:
        target.stepTrack(-1);
        return true;
    case InputKey::F5:
        target.stepTrack(1);
        return true;
    case InputKey::F6:
        target.toggleShuffle();
        return true;
    case InputKey::F7:
        target.toggleRepeatAll();
        return true;
    case InputKey::F8:
        target.toggleRepeatOne();
        return true;
    case InputKey::F9:
        target.openSearchPage();
        return true;
    case InputKey::F10:
        target.openLibraryPage();
        return true;
    case InputKey::F11:
        target.openQueuePage();
        return true;
    case InputKey::F12:
        target.openSettingsPage();
        return true;
    default:
        return false;
    }
}

} // namespace

void routeInput(AppInputTarget& target, const InputEvent& event)
{
    const auto action = mapInput(event);
    const auto page = target.currentPage();

    if (page == AppPage::Boot) {
        return;
    }

    if (event.key == InputKey::F1) {
        target.toggleHelpForCurrentPage();
        return;
    }

    if (target.helpOpen()) {
        if (action == UserAction::Back || event.key == InputKey::Enter) {
            target.closeHelp();
        }
        return;
    }

    if (page == AppPage::RemoteFieldEditor) {
        if (event.key == InputKey::Character && !event.text.empty()) {
            target.appendRemoteProfileEditText(event.text);
        } else if (event.key == InputKey::Backspace) {
            target.backspaceRemoteProfileEdit();
        } else if (event.key == InputKey::Enter) {
            target.commitRemoteProfileEdit();
        }
        return;
    }

    if (routeGlobalTransportShortcut(target, event)) {
        return;
    }

    if (page != AppPage::Search && isAudioEffectShortcut(event)) {
        target.cycleAudioEffect();
        return;
    }

    if (page == AppPage::NowPlaying) {
        if (action == UserAction::Back || action == UserAction::Home) {
            if (action == UserAction::Home) {
                target.showMainMenuPage();
            } else {
                target.popPage();
            }
        } else if (isLyricsToggle(event)) {
            target.pushPage(AppPage::Lyrics);
        } else if (isQueueToggle(event)) {
            target.pushPage(AppPage::Queue);
        } else if (action == UserAction::Left) {
            target.stepTrack(-1);
        } else if (action == UserAction::Right || action == UserAction::NextTrack) {
            target.stepTrack(1);
        } else if (action == UserAction::Up) {
            target.toggleShuffle();
        } else if (action == UserAction::Down) {
            target.cycleRepeatMode();
        } else if (action == UserAction::Confirm && !target.nowPlayingConfirmBlocked()) {
            target.togglePlayPause();
        }
        return;
    }

    if (page == AppPage::Lyrics) {
        if (action == UserAction::Back || action == UserAction::Home) {
            if (action == UserAction::Home) {
                target.showMainMenuPage();
            } else {
                target.popPage();
            }
        } else if (isLyricsToggle(event)) {
            target.popPage();
        } else if (action == UserAction::Left) {
            target.stepTrack(-1);
        } else if (action == UserAction::Right || action == UserAction::NextTrack) {
            target.stepTrack(1);
        }
        return;
    }

    if (page == AppPage::Equalizer) {
        if (action == UserAction::Back || action == UserAction::Home) {
            if (action == UserAction::Home) {
                target.showMainMenuPage();
            } else {
                target.popPage();
            }
        } else if (action == UserAction::Left) {
            target.moveEqualizerSelection(-1);
        } else if (action == UserAction::Right) {
            target.moveEqualizerSelection(1);
        } else if (action == UserAction::Up) {
            target.adjustSelectedEqualizerBand(1);
        } else if (action == UserAction::Down) {
            target.adjustSelectedEqualizerBand(-1);
        } else if (action == UserAction::PageUp) {
            target.adjustSelectedEqualizerBand(3);
        } else if (action == UserAction::PageDown) {
            target.adjustSelectedEqualizerBand(-3);
        } else if (action == UserAction::Confirm) {
            target.cycleEqualizerPreset(1);
        }
        return;
    }

    if (page == AppPage::Search) {
        if (event.key == InputKey::Character && !event.text.empty()) {
            target.appendSearchText(event.text);
            return;
        }
        if (event.key == InputKey::Backspace) {
            target.backspaceSearchQuery();
            return;
        }
    }

    if (target.isBrowseListPage()) {
        if ((event.key == InputKey::Insert || isPlaylistEditShortcut(event)) && (page == AppPage::Playlists || page == AppPage::PlaylistDetail)) {
            target.pushPage(AppPage::PlaylistEditor);
            return;
        }
        if (isSortShortcut(event) && (page == AppPage::Songs || page == AppPage::PlaylistDetail)) {
            target.cycleSongSortModeAndClamp();
            return;
        }
    }

    if (page == AppPage::MainMenu) {
        if (action == UserAction::Left) {
            target.moveMainMenuSelection(-1);
        } else if (action == UserAction::Right) {
            target.moveMainMenuSelection(1);
        } else if (action == UserAction::PageUp) {
            target.moveMainMenuSelection(-3);
        } else if (action == UserAction::PageDown) {
            target.moveMainMenuSelection(3);
        } else if (action == UserAction::Home) {
            target.resetMainMenuSelection();
        } else if (action == UserAction::Confirm) {
            target.confirmMainMenu();
        }
    } else if (action == UserAction::Back || action == UserAction::Home) {
        if (action == UserAction::Home) {
            target.showMainMenuPage();
            return;
        }
        target.popPage();
    } else if (action == UserAction::Up) {
        target.moveSelection(-1);
    } else if (action == UserAction::Down) {
        target.moveSelection(1);
    } else if (action == UserAction::PageUp) {
        target.moveSelectionPage(-1);
    } else if (action == UserAction::PageDown) {
        target.moveSelectionPage(1);
    } else if (action == UserAction::Confirm) {
        target.confirmListPage();
    }
}

} // namespace lofibox::app
