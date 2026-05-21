// SPDX-License-Identifier: GPL-3.0-or-later

#include "app/app_renderer.h"

#include <algorithm>
#include <cstdint>
#include <string>

#include "app/app_projection_builder.h"
#include "core/display_profile.h"
#include "ui/pages/about_page.h"
#include "ui/pages/equalizer_page.h"
#include "ui/pages/list_page.h"
#include "ui/pages/lyrics_page.h"
#include "ui/pages/main_menu_page.h"
#include "ui/pages/now_playing_page.h"
#include "ui/ui_primitives.h"

namespace lofibox::app {
namespace {

namespace ui_pages = lofibox::ui::pages;
using clock = std::chrono::steady_clock;

std::vector<std::pair<std::string_view, std::string_view>> helpRowsForPage(AppPage page)
{
    switch (page) {
    case AppPage::MainMenu:
        return {
            {"LEFT/RIGHT", "CHOOSE PAGE"},
            {"PGUP/PGDN", "JUMP PAGE"},
            {"OK", "OPEN"},
            {"F2/F3", "PLAY / PAUSE"},
            {"F4/F5", "PREV / NEXT"},
            {"F6", "PLAY MODE"},
            {"R", "REMIX MODE"},
            {"F9-F12", "SEARCH LIB QUEUE SET"},
        };
    case AppPage::Songs:
    case AppPage::PlaylistDetail:
        return {
            {"UP/DOWN", "MOVE"},
            {"PGUP/PGDN", "PAGE"},
            {"OK", "PLAY"},
            {"BACKSPACE", "BACK"},
            {"T", "SORT"},
            {"E/INS", "EDIT PLAYLIST"},
            {"F2-F6", "PLAYBACK"},
            {"R", "REMIX MODE"},
            {"F9", "SEARCH"},
        };
    case AppPage::NowPlaying:
        return {
            {"OK", "PLAY / PAUSE"},
            {"LEFT", "PREVIOUS"},
            {"RIGHT", "NEXT"},
            {"UP/DOWN", "PLAY MODE"},
            {"L", "LYRICS"},
            {"Q", "QUEUE"},
            {"F2-F6", "PLAYBACK"},
            {"R", "REMIX MODE"},
            {"HOME", "MENU"},
        };
    case AppPage::Lyrics:
        return {
            {"L", "NOW PLAYING"},
            {"LEFT/RIGHT", "PREV / NEXT"},
            {"BACKSPACE", "BACK"},
            {"F2-F6", "PLAYBACK"},
            {"R", "REMIX MODE"},
            {"HOME", "MENU"},
        };
    case AppPage::Equalizer:
        return {
            {"LEFT/RIGHT", "BAND"},
            {"UP/DOWN", "+/- 1DB"},
            {"PGUP/PGDN", "+/- 3DB"},
            {"OK", "PRESET"},
            {"F2-F6", "PLAYBACK"},
            {"R", "REMIX MODE"},
            {"HOME", "MENU"},
        };
    case AppPage::Search:
        return {
            {"TYPE", "QUERY"},
            {"BACKSPACE", "EDIT"},
            {"UP/DOWN", "MOVE"},
            {"PGUP/PGDN", "PAGE"},
            {"OK", "PLAY"},
            {"F2-F6", "PLAYBACK"},
            {"HOME", "MENU"},
        };
    case AppPage::RemoteFieldEditor:
        return {
            {"TYPE", "VALUE"},
            {"BACKSPACE", "EDIT"},
            {"OK", "SAVE"},
            {"F1", "HELP"},
        };
    case AppPage::PlaylistEditor:
        return {
            {"UP/DOWN", "MOVE"},
            {"PGUP/PGDN", "PAGE"},
            {"OK", "OPEN"},
            {"DEL", "REMOVE"},
            {"BACKSPACE", "BACK"},
            {"F2-F6", "PLAYBACK"},
            {"R", "REMIX MODE"},
            {"HOME", "MENU"},
        };
    case AppPage::MusicIndex:
    case AppPage::Artists:
    case AppPage::Albums:
    case AppPage::Genres:
    case AppPage::Composers:
    case AppPage::Compilations:
    case AppPage::Playlists:
    case AppPage::RemoteSetup:
    case AppPage::SourceManager:
    case AppPage::RemoteProfileSettings:
    case AppPage::Queue:
    case AppPage::RemoteBrowse:
    case AppPage::ServerDiagnostics:
    case AppPage::StreamDetail:
        return {
            {"UP/DOWN", "MOVE"},
            {"PGUP/PGDN", "PAGE"},
            {"OK", "OPEN"},
            {"BACKSPACE", "BACK"},
            {"F2-F6", "PLAYBACK"},
            {"R", "REMIX MODE"},
            {"F9-F12", "SEARCH LIB QUEUE SET"},
            {"HOME", "MENU"},
        };
    case AppPage::Boot:
    case AppPage::Settings:
        return {
            {"UP/DOWN", "MOVE"},
            {"PGUP/PGDN", "PAGE"},
            {"OK", "OPEN"},
            {"BACKSPACE", "BACK"},
            {"F2-F6", "PLAYBACK"},
            {"R", "REMIX MODE"},
            {"F9", "SEARCH"},
            {"HOME", "MENU"},
        };
    case AppPage::About:
        return {};
    }
    return {};
}

void renderHelpIfOpen(core::Canvas& canvas, const AppRenderTarget& target)
{
    if (!target.helpOpen() || target.helpPage() == AppPage::Boot) {
        return;
    }
    const auto& theme = target.theme();
    const auto title = target.helpPage() == AppPage::MainMenu ? std::string_view{"MENU SHORTCUTS"} : std::string_view{"SHORTCUTS"};
    ui::drawPageHelpModal(canvas, theme, title, helpRowsForPage(target.helpPage()));
}

std::string trimBootText(std::string value, std::size_t max_size)
{
    if (value.size() <= max_size) {
        return value;
    }
    if (max_size <= 3U) {
        return value.substr(0, max_size);
    }
    return "..." + value.substr(value.size() - (max_size - 3U));
}

std::string bootStatusText(LibraryIndexState state, const LibraryScanProgress& progress)
{
    if (state == LibraryIndexState::Uninitialized) {
        return "STARTING";
    }
    if (state != LibraryIndexState::Loading) {
        return state == LibraryIndexState::Degraded ? "LIBRARY DEGRADED" : "LIBRARY READY";
    }

    switch (progress.phase) {
    case LibraryScanPhase::DiscoveringFiles: return "SCANNING FILES";
    case LibraryScanPhase::ReadingMetadata: return "READING METADATA";
    case LibraryScanPhase::BuildingIndexes: return "BUILDING INDEXES";
    case LibraryScanPhase::Complete: return "LIBRARY READY";
    case LibraryScanPhase::Failed: return "LIBRARY DEGRADED";
    case LibraryScanPhase::Idle: break;
    }
    return "LOADING LIBRARY";
}

std::string bootDetailText(const LibraryScanProgress& progress)
{
    switch (progress.phase) {
    case LibraryScanPhase::DiscoveringFiles:
        return "ROOTS " + std::to_string(progress.roots_scanned) + "/" + std::to_string(progress.roots_total)
            + "  FILES " + std::to_string(progress.files_discovered);
    case LibraryScanPhase::ReadingMetadata:
        return "FILES " + std::to_string(progress.files_processed) + "/" + std::to_string(progress.files_total)
            + "  TRACKS " + std::to_string(progress.tracks_indexed);
    case LibraryScanPhase::BuildingIndexes:
        return "TRACKS " + std::to_string(progress.tracks_indexed);
    case LibraryScanPhase::Failed:
        return progress.message.empty() ? "SCAN FAILED" : trimBootText(progress.message, 42U);
    case LibraryScanPhase::Complete:
        return "TRACKS " + std::to_string(progress.tracks_indexed);
    case LibraryScanPhase::Idle:
        break;
    }
    return {};
}

float bootProgressRatio(const LibraryScanProgress& progress)
{
    int total = 0;
    int done = 0;
    if (progress.phase == LibraryScanPhase::ReadingMetadata && progress.files_total > 0) {
        total = progress.files_total;
        done = progress.files_processed;
    } else if (progress.phase == LibraryScanPhase::DiscoveringFiles && progress.roots_total > 0) {
        total = progress.roots_total;
        done = progress.roots_scanned;
    } else if (progress.phase == LibraryScanPhase::BuildingIndexes || progress.phase == LibraryScanPhase::Complete) {
        total = 1;
        done = progress.phase == LibraryScanPhase::Complete ? 1 : 0;
    }
    if (total <= 0) {
        return 0.0F;
    }
    return std::clamp(static_cast<float>(done) / static_cast<float>(total), 0.0F, 1.0F);
}

void drawBootProgress(core::Canvas& canvas, const ui::UiTheme& theme, const LibraryScanProgress& progress)
{
    const float ratio = bootProgressRatio(progress);
    constexpr int width = 180;
    constexpr int height = 4;
    const int x = (core::kDisplayWidth - width) / 2;
    constexpr int y = 172;
    canvas.fillRect(x, y, width, height, theme.palette.panel2);
    canvas.fillRect(x, y, static_cast<int>(static_cast<float>(width) * ratio), height, theme.palette.progress);
}

void renderBootPage(core::Canvas& canvas, const AppRenderTarget& target)
{
    const auto& theme = target.theme();
    canvas.fillRect(0, 0, core::kDisplayWidth, core::kDisplayHeight, theme.palette.background);
    const auto progress = target.libraryScanProgress();
    const std::string status = bootStatusText(target.libraryState(), progress);
    if (target.assets().logo) {
        const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(clock::now() - target.bootStarted());
        const float t = std::clamp(static_cast<float>(elapsed.count()) / 1200.0f, 0.0f, 1.0f);
        const auto opacity = static_cast<std::uint8_t>(std::clamp(72.0f + (t * 183.0f), 72.0f, 255.0f));
        constexpr int size = 122;
        const int x = (core::kDisplayWidth - size) / 2;
        constexpr int y = 18;
        ui::blitScaledCanvas(canvas, *target.assets().logo, x, y, size, size, opacity);
    } else {
        ui::drawText(canvas, "LOFIBOX ZERO", ui::centeredX("LOFIBOX ZERO", 2), 38, theme.palette.text_primary, 2);
    }
    ui::drawText(canvas, status, ui::centeredX(status, 1), 144, theme.palette.text_secondary, 1);
    const auto detail = bootDetailText(progress);
    if (!detail.empty()) {
        ui::drawText(canvas, detail, ui::centeredX(detail, 1), 158, theme.palette.text_muted, 1);
        drawBootProgress(canvas, theme, progress);
    }
    if (!progress.current_path.empty() && progress.phase == LibraryScanPhase::ReadingMetadata) {
        const auto path = trimBootText(progress.current_path, 48U);
        ui::drawText(canvas, path, ui::centeredX(path, 1), 182, theme.palette.text_muted, 1);
    }
}

void renderMainMenu(core::Canvas& canvas, const AppRenderTarget& target)
{
    const auto& theme = target.theme();
    ui_pages::renderMainMenuPage(
        canvas,
        buildMainMenuProjection(target),
        theme);
    renderHelpIfOpen(canvas, target);
}

void renderNowPlaying(core::Canvas& canvas, const AppRenderTarget& target)
{
    const auto& theme = target.theme();
    ui::drawListPageFrame(canvas, theme);
    const auto& playback = target.playbackSession();
    std::string source_label = playback.current_stream_source;
    if (playback.current_track_id) {
        if (const auto* track = target.findTrack(*playback.current_track_id); track && track->remote) {
            source_label = track->source_label;
        } else {
            source_label.clear();
        }
    }
    ui::drawTopBar(canvas, theme, target.pageModel().title, true, {}, source_label);
    ui_pages::renderNowPlayingPage(
        canvas,
        buildNowPlayingProjection(target),
        theme);
    renderHelpIfOpen(canvas, target);
}

void renderLyrics(core::Canvas& canvas, const AppRenderTarget& target)
{
    const auto& theme = target.theme();
    ui::drawListPageFrame(canvas, theme);
    ui::drawTopBar(canvas, theme, target.pageModel().title, true);
    ui_pages::renderLyricsPage(
        canvas,
        buildLyricsProjection(target),
        theme);
    renderHelpIfOpen(canvas, target);
}

void renderList(core::Canvas& canvas, const AppRenderTarget& target)
{
    const auto& theme = target.theme();
    ui_pages::renderListPage(
        canvas,
        buildListProjection(target),
        theme);
    renderHelpIfOpen(canvas, target);
}

} // namespace

void renderApp(core::Canvas& canvas, const AppRenderTarget& target)
{
    const auto& theme = target.theme();
    canvas.clear(theme.palette.background);

    switch (target.currentPage()) {
    case AppPage::Boot:
        renderBootPage(canvas, target);
        return;
    case AppPage::MainMenu:
        renderMainMenu(canvas, target);
        return;
    case AppPage::NowPlaying:
        renderNowPlaying(canvas, target);
        return;
    case AppPage::Lyrics:
        renderLyrics(canvas, target);
        return;
    case AppPage::Equalizer:
        ui_pages::renderEqualizerPage(canvas, buildEqualizerProjection(target), theme);
        renderHelpIfOpen(canvas, target);
        return;
    case AppPage::About:
        ui_pages::renderAboutPage(canvas, buildAboutProjection(target), theme);
        renderHelpIfOpen(canvas, target);
        return;
    case AppPage::MusicIndex:
    case AppPage::Artists:
    case AppPage::Albums:
    case AppPage::Songs:
    case AppPage::Genres:
    case AppPage::Composers:
    case AppPage::Compilations:
    case AppPage::Playlists:
    case AppPage::PlaylistDetail:
    case AppPage::Settings:
    case AppPage::RemoteSetup:
    case AppPage::SourceManager:
    case AppPage::RemoteProfileSettings:
    case AppPage::RemoteFieldEditor:
    case AppPage::Search:
    case AppPage::Queue:
    case AppPage::RemoteBrowse:
    case AppPage::ServerDiagnostics:
    case AppPage::StreamDetail:
    case AppPage::PlaylistEditor:
        renderList(canvas, target);
        return;
    }
}

} // namespace lofibox::app
