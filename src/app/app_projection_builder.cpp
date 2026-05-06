// SPDX-License-Identifier: GPL-3.0-or-later

#include "app/app_projection_builder.h"

#include <chrono>
#include <string>

#include "app/app_renderer.h"

namespace lofibox::app {
namespace {

namespace ui_pages = lofibox::ui::pages;

constexpr std::string_view kNoMusic = "NO MUSIC";
constexpr std::string_view kNoAlbums = "NO ALBUMS";
constexpr std::string_view kNoSongs = "NO SONGS";
constexpr std::string_view kNoGenres = "NO GENRES";
constexpr std::string_view kNoComposers = "NO COMPOSERS";
constexpr std::string_view kNoCompilations = "NO COMPILATIONS";
constexpr std::string_view kEmpty = "EMPTY";
#if defined(LOFIBOX_VERSION)
constexpr std::string_view kVersion{LOFIBOX_VERSION};
#else
constexpr std::string_view kVersion{"unknown"};
#endif

std::string formatStorage(const StorageInfo& storage)
{
    if (!storage.available || storage.capacity_bytes == 0) {
        return "N/A";
    }
    const auto used = storage.capacity_bytes - storage.free_bytes;
    const auto used_mb = static_cast<int>(used / (1024 * 1024));
    const auto total_mb = static_cast<int>(storage.capacity_bytes / (1024 * 1024));
    return std::to_string(used_mb) + "/" + std::to_string(total_mb) + "MB";
}

ui::SpectrumFrame toUiSpectrumFrame(const AudioVisualizationFrame& frame)
{
    ui::SpectrumFrame view{};
    view.available = frame.available;
    view.bands = frame.bands;
    return view;
}

ui::LyricsContent toUiLyricsContent(const TrackLyrics& lyrics)
{
    ui::LyricsContent view{};
    view.plain = lyrics.plain;
    view.synced = lyrics.synced;
    return view;
}

ui_pages::NowPlayingStatus toUiPlaybackStatus(PlaybackStatus status)
{
    switch (status) {
    case PlaybackStatus::Empty: return ui_pages::NowPlayingStatus::Empty;
    case PlaybackStatus::Paused: return ui_pages::NowPlayingStatus::Paused;
    case PlaybackStatus::Playing: return ui_pages::NowPlayingStatus::Playing;
    }
    return ui_pages::NowPlayingStatus::Empty;
}

ui_pages::MenuIndexState toUiIndexState(LibraryIndexState state)
{
    switch (state) {
    case LibraryIndexState::Uninitialized: return ui_pages::MenuIndexState::Uninitialized;
    case LibraryIndexState::Loading: return ui_pages::MenuIndexState::Loading;
    case LibraryIndexState::Ready: return ui_pages::MenuIndexState::Ready;
    case LibraryIndexState::Degraded: return ui_pages::MenuIndexState::Degraded;
    }
    return ui_pages::MenuIndexState::Uninitialized;
}

std::string playbackSummary(const AppRenderTarget& target)
{
    const auto& playback = target.playbackSession();
    if (!playback.current_track_id) {
        if (!playback.current_stream_title.empty()) {
            std::string summary = playback.status == PlaybackStatus::Paused ? "PAUSED  " : "PLAYING  ";
            summary += playback.current_stream_title;
            if (!playback.current_stream_artist.empty()) {
                summary += " - ";
                summary += playback.current_stream_artist;
            }
            return summary;
        }
        return "NO TRACK";
    }
    const auto* track = target.findTrack(*playback.current_track_id);
    if (track == nullptr) {
        return "NO TRACK";
    }
    std::string summary = playback.status == PlaybackStatus::Paused ? "PAUSED  " : "PLAYING  ";
    summary += track->title.empty() ? "UNKNOWN" : track->title;
    if (!track->artist.empty() && track->artist != "UNKNOWN") {
        summary += " - ";
        summary += track->artist;
    }
    return summary;
}

std::string emptyLabelForPage(AppPage page)
{
    switch (page) {
    case AppPage::Artists: return std::string(kNoMusic);
    case AppPage::Albums: return std::string(kNoAlbums);
    case AppPage::Songs: return std::string(kNoSongs);
    case AppPage::Genres: return std::string(kNoGenres);
    case AppPage::Composers: return std::string(kNoComposers);
    case AppPage::Compilations: return std::string(kNoCompilations);
    case AppPage::PlaylistDetail: return std::string(kEmpty);
    case AppPage::RemoteBrowse: return "NO ITEMS";
    default: return std::string(kEmpty);
    }
}

} // namespace

ui::pages::MainMenuView buildMainMenuProjection(const AppRenderTarget& target)
{
    return ui_pages::MainMenuView{
        target.mainMenuIndex(),
        ui_pages::MenuStorageView{target.storage().available, target.storage().capacity_bytes, target.storage().free_bytes},
        toUiIndexState(target.libraryState()),
        target.networkConnected(),
        &target.assets(),
        playbackSummary(target),
        static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count() / 45)};
}

ui::pages::NowPlayingView buildNowPlayingProjection(const AppRenderTarget& target)
{
    const auto& playback = target.playbackSession();
    const TrackRecord* track = playback.current_track_id ? target.findTrack(*playback.current_track_id) : nullptr;
    const bool has_remote_stream = track == nullptr && !playback.current_stream_title.empty();
    return ui_pages::NowPlayingView{
        track != nullptr || has_remote_stream,
        track ? track->title : playback.current_stream_title,
        track ? track->artist : playback.current_stream_artist,
        track ? track->album : playback.current_stream_album,
        track ? track->duration_seconds : playback.current_stream_duration_seconds,
        playback.elapsed_seconds,
        toUiPlaybackStatus(playback.status),
        playback.shuffle_enabled,
        playback.repeat_all,
        playback.repeat_one,
        playback.current_artwork ? &*playback.current_artwork : nullptr,
        toUiSpectrumFrame(playback.visualization_frame)};
}

ui::pages::LyricsPageView buildLyricsProjection(const AppRenderTarget& target)
{
    const auto& playback = target.playbackSession();
    const TrackRecord* track = playback.current_track_id ? target.findTrack(*playback.current_track_id) : nullptr;
    const bool has_remote_stream = track == nullptr && !playback.current_stream_title.empty();
    return ui_pages::LyricsPageView{
        track != nullptr || has_remote_stream,
        track ? track->title : playback.current_stream_title,
        track ? track->artist : playback.current_stream_artist,
        track ? track->duration_seconds : playback.current_stream_duration_seconds,
        playback.elapsed_seconds,
        toUiPlaybackStatus(playback.status),
        playback.lyrics_lookup_pending,
        toUiLyricsContent(playback.current_lyrics),
        toUiSpectrumFrame(playback.visualization_frame)};
}

ui::pages::ListPageView buildListProjection(const AppRenderTarget& target)
{
    const auto model = target.pageModel();
    const auto& rows = model.rows;
    const auto page = target.currentPage();
    return ui_pages::ListPageView{
        model.title,
        !model.browse_list,
        model.browse_list ? "F1:HELP" : "",
        rows,
        rows.empty() ? emptyLabelForPage(page) : std::string{},
        target.listSelection().selected,
        target.listSelection().scroll,
        false};
}

ui::pages::AboutPageView buildAboutProjection(const AppRenderTarget& target)
{
    return ui_pages::AboutPageView{std::string(kVersion), formatStorage(target.storage())};
}

ui::pages::EqualizerPageView buildEqualizerProjection(const AppRenderTarget& target)
{
    const auto eq = target.eqRuntimeSnapshot();
    return ui_pages::EqualizerPageView{eq.bands, target.eqState().selected_band, eq.preset_name};
}

} // namespace lofibox::app
