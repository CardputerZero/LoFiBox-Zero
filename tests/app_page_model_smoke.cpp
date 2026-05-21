// SPDX-License-Identifier: GPL-3.0-or-later

#include <iostream>

#include "app/app_page_model.h"

int main()
{
    lofibox::app::AppPageModelInput input{};

    input.page = lofibox::app::AppPage::Settings;
    input.network_connected = true;
    input.metadata_display_name = "BUILT-IN";
    input.settings.backlight_index = 2;
    const auto settings_model = lofibox::app::buildAppPageModel(input);
    if (settings_model.title != "SETTINGS"
        || settings_model.rows.size() != 7
        || settings_model.rows[0].second != "ONLINE"
        || settings_model.rows[3].second != "3"
        || settings_model.rows[5].first != "REMOTE SETUP"
        || settings_model.rows[5].second != "15 TYPES") {
        std::cerr << "Expected Settings page model to own settings rows.\n";
        return 1;
    }

    input.webui_url = "http://localhost:8765";
    const auto webui_settings_model = lofibox::app::buildAppPageModel(input);
    if (webui_settings_model.rows.size() != 8
        || webui_settings_model.rows[5].first != "WEBUI"
        || webui_settings_model.rows[5].second != "http://localhost:8765"
        || webui_settings_model.rows[6].first != "REMOTE SETUP"
        || webui_settings_model.rows[7].first != "ABOUT") {
        std::cerr << "Expected Settings page model to expose WebUI address when online.\n";
        return 1;
    }
    input.network_connected = false;
    const auto offline_webui_settings_model = lofibox::app::buildAppPageModel(input);
    if (offline_webui_settings_model.rows.size() != 7 || offline_webui_settings_model.rows[5].first != "REMOTE SETUP") {
        std::cerr << "Expected Settings page model to hide WebUI address when network is offline.\n";
        return 1;
    }
    input.network_connected = true;
    input.webui_url.clear();

    input.page = lofibox::app::AppPage::Songs;
    input.library_title_override = "recently added";
    input.library_rows = std::vector<std::pair<std::string, std::string>>{{"Song A", "Artist A"}};
    const auto songs_model = lofibox::app::buildAppPageModel(input);
    if (songs_model.title != "RECENTLY ADDED" || !songs_model.browse_list || songs_model.rows.size() != 1) {
        std::cerr << "Expected library page model to preserve title override, rows, and browse-list state.\n";
        return 1;
    }

    input.page = lofibox::app::AppPage::NowPlaying;
    input.library_title_override.reset();
    input.library_rows.reset();
    const auto now_playing_model = lofibox::app::buildAppPageModel(input);
    if (now_playing_model.title != "NOW PLAYING" || now_playing_model.browse_list || now_playing_model.max_visible_rows != lofibox::app::kPageModelMaxVisibleRows) {
        std::cerr << "Expected non-list page defaults to be modeled consistently.\n";
        return 1;
    }

    input.page = lofibox::app::AppPage::SourceManager;
    input.source_manager_rows = std::vector<std::pair<std::string, std::string>>{{"ADD Navidrome", "opensubsonic"}};
    const auto source_model = lofibox::app::buildAppPageModel(input);
    if (source_model.title != "SOURCES" || !source_model.browse_list || source_model.rows.size() != 1) {
        std::cerr << "Expected Source Manager to be a projected browse list.\n";
        return 1;
    }

    input.page = lofibox::app::AppPage::RemoteSetup;
    input.source_manager_rows.reset();
    const auto remote_setup_model = lofibox::app::buildAppPageModel(input);
    if (remote_setup_model.title != "REMOTE SETUP"
        || !remote_setup_model.browse_list
        || remote_setup_model.rows.size() < 15U
        || remote_setup_model.rows.front().first != "Jellyfin") {
        std::cerr << "Expected Remote Setup to list supported remote media types before profile fields.\n";
        return 1;
    }

    return 0;
}
