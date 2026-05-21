// SPDX-License-Identifier: GPL-3.0-or-later

#include "app/app_page_model.h"

#include <string_view>

#include "app/settings_projection_builder.h"
#include "remote/common/remote_source_registry.h"
#include "ui/ui_primitives.h"

namespace lofibox::app {
namespace {

std::string_view pageTitleDefault(AppPage page) noexcept
{
    switch (page) {
    case AppPage::Boot: return "LOADING";
    case AppPage::MainMenu: return "LOFIBOX";
    case AppPage::MusicIndex: return "LIBRARY";
    case AppPage::Artists: return "ARTISTS";
    case AppPage::Albums: return "ALBUMS";
    case AppPage::Songs: return "SONGS";
    case AppPage::Genres: return "GENRES";
    case AppPage::Composers: return "COMPOSERS";
    case AppPage::Compilations: return "COMPILATIONS";
    case AppPage::Playlists: return "PLAYLISTS";
    case AppPage::PlaylistDetail: return "PLAYLIST";
    case AppPage::NowPlaying: return "NOW PLAYING";
    case AppPage::Lyrics: return "LYRICS";
    case AppPage::Equalizer: return "EQUALIZER";
    case AppPage::Settings: return "SETTINGS";
    case AppPage::RemoteSetup: return "REMOTE SETUP";
    case AppPage::SourceManager: return "SOURCES";
    case AppPage::RemoteProfileSettings: return "PROFILE SETUP";
    case AppPage::RemoteFieldEditor: return "EDIT REMOTE";
    case AppPage::Search: return "SEARCH";
    case AppPage::Queue: return "UP NEXT";
    case AppPage::RemoteBrowse: return "REMOTE";
    case AppPage::ServerDiagnostics: return "SERVER";
    case AppPage::StreamDetail: return "STREAM";
    case AppPage::PlaylistEditor: return "PLAYLIST EDIT";
    case AppPage::About: return "ABOUT";
    }
    return "";
}

std::vector<std::pair<std::string, std::string>> settingsRows(const AppPageModelInput& input)
{
    return buildSettingsProjectionRows(SettingsProjectionInput{
        input.network_connected,
        input.metadata_display_name,
        input.settings.sleep_timer_index,
        input.settings.backlight_index,
        true,
        static_cast<int>(remote::RemoteSourceRegistry{}.manifests().size()),
        input.webui_url});
}

} // namespace

bool isBrowseListPage(AppPage page) noexcept
{
    switch (page) {
    case AppPage::MusicIndex:
    case AppPage::Artists:
    case AppPage::Albums:
    case AppPage::Songs:
    case AppPage::Genres:
    case AppPage::Composers:
    case AppPage::Compilations:
    case AppPage::Playlists:
    case AppPage::PlaylistDetail:
    case AppPage::SourceManager:
    case AppPage::RemoteSetup:
    case AppPage::RemoteProfileSettings:
    case AppPage::RemoteFieldEditor:
    case AppPage::Search:
    case AppPage::Queue:
    case AppPage::RemoteBrowse:
    case AppPage::ServerDiagnostics:
    case AppPage::StreamDetail:
    case AppPage::PlaylistEditor:
        return true;
    default:
        return false;
    }
}

AppPageModel buildAppPageModel(const AppPageModelInput& input)
{
    AppPageModel model{};
    model.title = input.library_title_override ? ui::fitUpper(*input.library_title_override, 18) : std::string(pageTitleDefault(input.page));
    model.browse_list = isBrowseListPage(input.page);

    if (input.library_rows) {
        model.rows = *input.library_rows;
        return model;
    }
    if (input.page_rows) {
        model.rows = *input.page_rows;
        return model;
    }

    if (input.page == AppPage::Settings) {
        model.rows = settingsRows(input);
    }
    if (input.page == AppPage::SourceManager && input.source_manager_rows) {
        model.rows = *input.source_manager_rows;
    }
    if (input.page == AppPage::RemoteSetup) {
        for (const auto& manifest : remote::RemoteSourceRegistry{}.manifests()) {
            model.rows.emplace_back(manifest.display_name, manifest.family);
        }
    }
    if (input.page == AppPage::Search) {
        model.rows = {{"TYPE QUERY", "F9"}, {"LOCAL + REMOTE", "GROUPED"}, {"RESULT SOURCE", "VISIBLE"}};
    }
    if (input.page == AppPage::Queue) {
        model.rows = {{"CURRENT QUEUE", "MIXED"}, {"LOCAL TRACKS", "OK"}, {"REMOTE TRACKS", "OK"}};
    }
    if (input.page == AppPage::RemoteBrowse) {
        model.rows = {{"ARTISTS", "BROWSE"}, {"ALBUMS", "BROWSE"}, {"PLAYLISTS", "BROWSE"}, {"FOLDERS", "BROWSE"}, {"GENRES", "BROWSE"}, {"FAVORITES", "BROWSE"}};
    }
    if (input.page == AppPage::ServerDiagnostics) {
        model.rows = {{"CONNECTION", input.network_connected ? "ONLINE" : "OFFLINE"}, {"TLS", "VERIFY"}, {"PERMISSIONS", "READ ONLY"}, {"TOKEN", "REDACTED"}};
    }
    if (input.page == AppPage::StreamDetail) {
        model.rows = {{"SOURCE", "REMOTE"}, {"URL", "REDACTED"}, {"BUFFER", "VISIBLE"}, {"QUALITY", "AUTO"}, {"CODEC", "UNKNOWN"}};
    }
    if (input.page == AppPage::PlaylistEditor) {
        model.rows = {{"ADD TRACK", "OK"}, {"REMOVE", "DEL"}, {"REORDER", "UP/DOWN"}, {"SAVE", "OK"}};
    }

    return model;
}

} // namespace lofibox::app
