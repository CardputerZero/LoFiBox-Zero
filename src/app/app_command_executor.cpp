// SPDX-License-Identifier: GPL-3.0-or-later

#include "app/app_command_executor.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <string_view>

namespace lofibox::app {
namespace {

constexpr std::array<AppPage, 6> kMainMenuPages{
    AppPage::Songs,
    AppPage::MusicIndex,
    AppPage::Playlists,
    AppPage::NowPlaying,
    AppPage::Equalizer,
    AppPage::Settings,
};
bool selectedSettingsRowIs(AppCommandTarget& target, int selected, std::string_view name)
{
    const auto model = target.pageModel();
    if (selected < 0 || selected >= static_cast<int>(model.rows.size())) {
        return false;
    }
    return model.rows[static_cast<std::size_t>(selected)].first == name;
}

void clampListSelection(AppCommandTarget& target)
{
    const auto model = target.pageModel();
    target.navigationState().clampListSelection(static_cast<int>(model.rows.size()), model.max_visible_rows);
}

} // namespace

void commandPushPage(AppCommandTarget& target, AppPage page)
{
    target.closeHelpForCommand();
    target.navigationState().pushPage(page);
}

void commandPopPage(AppCommandTarget& target)
{
    target.closeHelpForCommand();
    target.navigationState().popPage();
}

void commandPlayFromMenu(AppCommandTarget& target)
{
    (void)target.submitRuntimeCommand(::lofibox::runtime::RuntimeCommand{
        ::lofibox::runtime::RuntimeCommandKind::PlaybackPlay,
        {},
        ::lofibox::runtime::CommandOrigin::Gui});
}

void commandPausePlayback(AppCommandTarget& target)
{
    (void)target.submitRuntimeCommand(::lofibox::runtime::RuntimeCommand{
        ::lofibox::runtime::RuntimeCommandKind::PlaybackPause,
        {},
        ::lofibox::runtime::CommandOrigin::Gui});
}

void commandStepTrack(AppCommandTarget& target, int delta)
{
    (void)target.submitRuntimeCommand(::lofibox::runtime::RuntimeCommand{
        ::lofibox::runtime::RuntimeCommandKind::QueueStep,
        ::lofibox::runtime::RuntimeCommandPayload::queueStep(delta),
        ::lofibox::runtime::CommandOrigin::Gui});
}

void commandCycleMainMenuPlaybackMode(AppCommandTarget& target)
{
    (void)target.submitRuntimeCommand(::lofibox::runtime::RuntimeCommand{
        ::lofibox::runtime::RuntimeCommandKind::PlaybackCycleMainMenuMode,
        {},
        ::lofibox::runtime::CommandOrigin::Gui});
}

void commandToggleRepeatAll(AppCommandTarget& target)
{
    const bool enabled = !target.appServices().playbackStatus().session().repeat_all;
    (void)target.submitRuntimeCommand(::lofibox::runtime::RuntimeCommand{
        ::lofibox::runtime::RuntimeCommandKind::PlaybackSetRepeatAll,
        ::lofibox::runtime::RuntimeCommandPayload::enabled(enabled),
        ::lofibox::runtime::CommandOrigin::Gui});
    if (!enabled) {
        (void)target.submitRuntimeCommand(::lofibox::runtime::RuntimeCommand{
            ::lofibox::runtime::RuntimeCommandKind::PlaybackSetRepeatOne,
            ::lofibox::runtime::RuntimeCommandPayload::enabled(false),
            ::lofibox::runtime::CommandOrigin::Gui});
    }
}

void commandToggleRepeatOne(AppCommandTarget& target)
{
    const bool enabled = !target.appServices().playbackStatus().session().repeat_one;
    (void)target.submitRuntimeCommand(::lofibox::runtime::RuntimeCommand{
        ::lofibox::runtime::RuntimeCommandKind::PlaybackSetRepeatOne,
        ::lofibox::runtime::RuntimeCommandPayload::enabled(enabled),
        ::lofibox::runtime::CommandOrigin::Gui});
    if (!enabled) {
        (void)target.submitRuntimeCommand(::lofibox::runtime::RuntimeCommand{
            ::lofibox::runtime::RuntimeCommandKind::PlaybackSetRepeatAll,
            ::lofibox::runtime::RuntimeCommandPayload::enabled(false),
            ::lofibox::runtime::CommandOrigin::Gui});
    }
}

void commandMoveMainMenuSelection(AppCommandTarget& target, int delta)
{
    auto& index = target.mainMenuIndex();
    index = (index + static_cast<int>(kMainMenuPages.size()) + delta) % static_cast<int>(kMainMenuPages.size());
}

void commandResetMainMenuSelection(AppCommandTarget& target) noexcept
{
    target.mainMenuIndex() = 0;
}

void commandConfirmMainMenu(AppCommandTarget& target)
{
    const int index = target.mainMenuIndex();
    if (index < 0 || index >= static_cast<int>(kMainMenuPages.size())) {
        return;
    }
    if (kMainMenuPages[static_cast<std::size_t>(index)] == AppPage::Songs) {
        target.appServices().libraryMutations().setSongsContextAll();
    }
    commandPushPage(target, kMainMenuPages[static_cast<std::size_t>(index)]);
}

void commandToggleShuffle(AppCommandTarget& target)
{
    (void)target.submitRuntimeCommand(::lofibox::runtime::RuntimeCommand{
        ::lofibox::runtime::RuntimeCommandKind::PlaybackToggleShuffle,
        {},
        ::lofibox::runtime::CommandOrigin::Gui});
}

void commandCycleRepeatMode(AppCommandTarget& target)
{
    (void)target.submitRuntimeCommand(::lofibox::runtime::RuntimeCommand{
        ::lofibox::runtime::RuntimeCommandKind::PlaybackCycleRepeat,
        {},
        ::lofibox::runtime::CommandOrigin::Gui});
}

void commandTogglePlayPause(AppCommandTarget& target)
{
    (void)target.submitRuntimeCommand(::lofibox::runtime::RuntimeCommand{
        ::lofibox::runtime::RuntimeCommandKind::PlaybackToggle,
        {},
        ::lofibox::runtime::CommandOrigin::Gui});
}

void commandMoveEqualizerSelection(AppCommandTarget& target, int delta)
{
    auto& eq = target.eqState();
    eq.selected_band = std::clamp(eq.selected_band + delta, 0, static_cast<int>(target.eqRuntimeSnapshot().bands.size()) - 1);
}

void commandAdjustSelectedEqualizerBand(AppCommandTarget& target, int delta)
{
    (void)target.submitRuntimeCommand(::lofibox::runtime::RuntimeCommand{
        ::lofibox::runtime::RuntimeCommandKind::EqAdjustBand,
        ::lofibox::runtime::RuntimeCommandPayload::eqAdjustBand(target.eqState().selected_band, delta),
        ::lofibox::runtime::CommandOrigin::Gui});
}

void commandCycleEqualizerPreset(AppCommandTarget& target, int delta)
{
    (void)target.submitRuntimeCommand(::lofibox::runtime::RuntimeCommand{
        ::lofibox::runtime::RuntimeCommandKind::EqCyclePreset,
        ::lofibox::runtime::RuntimeCommandPayload::eqCyclePreset(delta),
        ::lofibox::runtime::CommandOrigin::Gui});
}

void commandCycleSongSortModeAndClamp(AppCommandTarget& target)
{
    target.appServices().libraryMutations().cycleSongSortMode();
    clampListSelection(target);
}

void commandMoveSelection(AppCommandTarget& target, int delta)
{
    const auto model = target.pageModel();
    target.navigationState().moveSelection(delta, static_cast<int>(model.rows.size()), model.max_visible_rows);
}

void commandMoveSelectionPage(AppCommandTarget& target, int delta_pages)
{
    const auto model = target.pageModel();
    const int count = static_cast<int>(model.rows.size());
    auto& navigation = target.navigationState();
    if (count <= 0) {
        navigation.list_selection = {};
        return;
    }
    const int step = std::max(1, model.max_visible_rows - 1);
    navigation.list_selection.selected = std::clamp(
        navigation.list_selection.selected + (delta_pages * step),
        0,
        count - 1);
    navigation.clampListSelection(count, model.max_visible_rows);
}

void commandConfirmListPage(AppCommandTarget& target)
{
    const int selected = target.navigationState().list_selection.selected;
    const auto page = target.currentPage();
    const auto library_result = target.appServices().libraryOpenActions().openSelectedListItem(page, selected);
    switch (library_result.kind) {
    case LibraryOpenResult::Kind::PushPage:
        commandPushPage(target, library_result.page);
        return;
    case LibraryOpenResult::Kind::StartTrack:
        if (target.startLibraryTrack(library_result.track_id) && target.currentPage() != AppPage::NowPlaying) {
            commandPushPage(target, AppPage::NowPlaying);
        }
        return;
    case LibraryOpenResult::Kind::None:
        break;
    }

    if (page == AppPage::MusicIndex && target.handleLibraryRemoteConfirm(selected)) {
        return;
    }

    if (page == AppPage::Settings && selectedSettingsRowIs(target, selected, "REMOTE SETUP")) {
        (void)target.handleSettingsRemoteConfirm(selected);
        return;
    }

    if (page == AppPage::RemoteSetup) {
        (void)target.handleRemoteSetupConfirm(selected);
        return;
    }

    if (page == AppPage::SourceManager) {
        if (!target.handleSourceManagerConfirm(selected)) {
            commandPushPage(target, selected == 0 ? AppPage::MusicIndex : AppPage::ServerDiagnostics);
        }
        return;
    }

    if (page == AppPage::RemoteProfileSettings) {
        (void)target.handleRemoteProfileSettingsConfirm(selected);
        return;
    }

    if (page == AppPage::RemoteBrowse && selected >= 0) {
        (void)target.handleRemoteBrowseConfirm(selected);
        return;
    }

    if (page == AppPage::StreamDetail) {
        if (target.handleStreamDetailConfirm()) {
            commandPushPage(target, AppPage::NowPlaying);
        }
        return;
    }

    if (page == AppPage::Search) {
        if (target.handleSearchConfirm(selected)) {
            commandPushPage(target, AppPage::NowPlaying);
        }
        return;
    }

    if (page == AppPage::Settings && selectedSettingsRowIs(target, selected, "ABOUT")) {
        commandPushPage(target, AppPage::About);
    }
}

} // namespace lofibox::app
