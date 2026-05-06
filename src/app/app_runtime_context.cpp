// SPDX-License-Identifier: GPL-3.0-or-later

#include "app/app_runtime_context.h"

#include <algorithm>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>

#include "app/source_manager_projection.h"
#include "app/text_input_utils.h"
#include "remote/common/remote_source_registry.h"
#include "remote/common/stream_source_model.h"
#include "runtime/runtime_command_client.h"

namespace lofibox::app {

namespace {
using clock = std::chrono::steady_clock;

RemoteServerKind kindForProtocol(StreamProtocol protocol) noexcept
{
    switch (protocol) {
    case StreamProtocol::Hls: return RemoteServerKind::Hls;
    case StreamProtocol::Dash: return RemoteServerKind::Dash;
    case StreamProtocol::Icecast:
    case StreamProtocol::Shoutcast:
        return RemoteServerKind::InternetRadio;
    case StreamProtocol::Smb: return RemoteServerKind::Smb;
    case StreamProtocol::Nfs: return RemoteServerKind::Nfs;
    case StreamProtocol::WebDav: return RemoteServerKind::WebDav;
    case StreamProtocol::Ftp: return RemoteServerKind::Ftp;
    case StreamProtocol::Sftp: return RemoteServerKind::Sftp;
    case StreamProtocol::DlnaUpnp: return RemoteServerKind::DlnaUpnp;
    case StreamProtocol::File:
    case StreamProtocol::Http:
    case StreamProtocol::Https:
        return RemoteServerKind::DirectUrl;
    }
    return RemoteServerKind::DirectUrl;
}

std::string qualityLabel(StreamQualityPreference preference)
{
    switch (preference) {
    case StreamQualityPreference::Original: return "ORIGINAL";
    case StreamQualityPreference::Auto: return "AUTO";
    case StreamQualityPreference::LowBandwidth: return "128K";
    case StreamQualityPreference::Manual: return "MANUAL";
    }
    return "AUTO";
}

constexpr int kRemoteProfileRowProfile = 0;
constexpr int kRemoteProfileRowKind = 1;
constexpr int kRemoteProfileRowName = 2;
constexpr int kRemoteProfileRowAddress = 3;
constexpr int kRemoteProfileRowUsername = 4;
constexpr int kRemoteProfileRowPassword = 5;
constexpr int kRemoteProfileRowToken = 6;
constexpr int kRemoteProfileRowTlsVerify = 7;
constexpr int kRemoteProfileRowSelfSigned = 8;
constexpr int kRemoteProfileRowPermission = 9;
constexpr int kRemoteProfileRowTest = 10;
constexpr int kRemoteProfileRowSave = 11;
constexpr int kUnifiedRemoteLibraryTrackLimit = 10000;

std::string remoteEditFieldName(int field)
{
    switch (field) {
    case kRemoteProfileRowName: return "LABEL";
    case kRemoteProfileRowAddress: return "ADDRESS";
    case kRemoteProfileRowUsername: return "USER";
    case kRemoteProfileRowPassword: return "PASSWORD";
    case kRemoteProfileRowToken: return "API TOKEN";
    }
    return "FIELD";
}

std::string hiddenSecretLabel(std::string_view value)
{
    return value.empty() ? "EMPTY" : "SET";
}

std::string searchItemKey(const MediaItem& item)
{
    if (item.local_track_id) {
        return "library:" + std::to_string(*item.local_track_id);
    }
    if (!item.remote_track_id.empty()) {
        return "remote:" + item.source.source_id + ":" + item.remote_track_id;
    }
    return item.id;
}

}

AppRuntimeContext::AppRuntimeContext(std::vector<std::filesystem::path> media_roots,
                                     ui::UiAssets assets,
                                     ui::UiTheme theme,
                                     ::lofibox::application::AppServiceHost& app_host,
                                     ::lofibox::runtime::RuntimeCommandClient& runtime_client,
                                     std::vector<std::string> startup_uris)
    : state_{},
      theme_(std::move(theme)),
      app_host_(app_host),
      runtime_client_(runtime_client)
{
    state_.media_roots = std::move(media_roots);
    state_.pending_open_uris = std::move(startup_uris);
    for (const auto& uri : state_.pending_open_uris) {
        std::error_code ec{};
        std::filesystem::path path{uri};
        if (!uri.empty() && uri.find("://") == std::string::npos && std::filesystem::exists(path, ec)) {
            state_.media_roots.push_back(path.parent_path());
        }
    }
    state_.ui_assets = std::move(assets);
    state_.remote_profiles = sourceProfileService().loadProfiles();
    refreshRuntimeStatus(true);
}

AppRuntimeContext::~AppRuntimeContext() = default;

void AppRuntimeContext::update()
{
    updateAppLifecycle(*this);
    handlePendingOpenRequests();
}

void AppRuntimeContext::handleInput(const InputEvent& event)
{
    routeInput(*this, event);
}

void AppRuntimeContext::render(core::Canvas& canvas) const
{
    renderApp(canvas, *this);
}

AppDebugSnapshot AppRuntimeContext::snapshot() const
{
    const auto services = appServices();
    const auto& playback = services.playbackStatus().session();
    const auto library_state = services.libraryQueries().state();
    return AppDebugSnapshot{
        currentPage(),
        library_state == LibraryIndexState::Ready || library_state == LibraryIndexState::Degraded,
        services.libraryQueries().model().tracks.size(),
        pageModel().rows.size(),
        state_.navigation.list_selection.selected,
        state_.navigation.list_selection.scroll,
        playback.current_track_id.has_value(),
        playback.status,
        playback.current_track_id,
        playback.shuffle_enabled,
        playback.repeat_all,
        playback.repeat_one,
        state_.network.connected,
        static_cast<int>(eqRuntimeSnapshot().bands.size()),
        state_.eq.selected_band};
}

bool AppRuntimeContext::bootAnimationComplete() const
{
    if (!state_.ui_assets.logo) {
        return true;
    }
    return (clock::now() - state_.boot_started) >= std::chrono::milliseconds{1450};
}

void AppRuntimeContext::refreshNetworkStatus()
{
    const auto diagnostics = appServices().diagnostics().snapshot();
    const auto capability = std::find_if(diagnostics.capabilities.begin(), diagnostics.capabilities.end(), [](const auto& item) {
        return item.name == "connectivity";
    });
    state_.network.connected = capability != diagnostics.capabilities.end() && capability->available;
    state_.network.status_message = state_.network.connected ? "ONLINE" : "OFFLINE";
}

void AppRuntimeContext::refreshMetadataServiceState()
{
    const auto diagnostics = appServices().diagnostics().snapshot();
    const auto capability = std::find_if(diagnostics.capabilities.begin(), diagnostics.capabilities.end(), [](const auto& item) {
        return item.name == "metadata";
    });
    state_.metadata_service.available = capability == diagnostics.capabilities.end() || capability->available;
    state_.metadata_service.display_name = capability == diagnostics.capabilities.end() ? "UNKNOWN" : capability->detail;
    state_.metadata_service.status = !state_.metadata_service.available ? "UNAVAILABLE" : (state_.network.connected ? "ONLINE" : "OFFLINE");
}

void AppRuntimeContext::refreshRuntimeStatus(bool force)
{
    const auto now = clock::now();
    if (!force && state_.last_status_refresh != clock::time_point{} && now - state_.last_status_refresh < std::chrono::seconds{2}) {
        return;
    }
    state_.last_status_refresh = now;
    refreshNetworkStatus();
    refreshMetadataServiceState();
}

void AppRuntimeContext::refreshRuntimeStatusIfDue()
{
    refreshRuntimeStatus(false);
}

void AppRuntimeContext::handlePendingOpenRequests()
{
    if (state_.pending_open_processed || state_.pending_open_uris.empty()) {
        return;
    }
    const auto uri = state_.pending_open_uris.front();
    if (uri.empty()) {
        state_.pending_open_processed = true;
        return;
    }

    if (uri.find("://") != std::string::npos) {
        const auto entry = remote::StreamSourceClassifier::classify(uri);
        RemoteServerProfile profile{};
        profile.kind = kindForProtocol(entry.protocol);
        profile.id = "desktop-open";
        profile.name = "DESKTOP OPEN";
        profile.base_url = uri;
        state_.selected_remote_kind = profile.kind;
        state_.selected_remote_profile_index.reset();
        state_.selected_transient_remote_profile = profile;
        state_.selected_remote_session = sourceProfileService().probe(profile, state_.remote_profiles.size()).session;
        RemoteTrack track{uri, "DESKTOP STREAM", "", "", "", 0};
        track.source_id = profile.id;
        track.source_label = profile.name;
        state_.selected_remote_node = RemoteCatalogNode{
            RemoteCatalogNodeKind::Tracks,
            uri,
            "DESKTOP STREAM",
            "",
            true,
            false};
        remoteBrowseService().rememberTrackFacts(profile, track);
        state_.selected_remote_stream = remoteBrowseService().resolve(profile, state_.selected_remote_session, track).stream;
        const auto payload = selectedRemoteStreamPayload();
        if (payload.get<::lofibox::runtime::RemotePlayResolvedStreamPayload>() != nullptr
            && submitRuntimeCommand(::lofibox::runtime::RuntimeCommand{
                ::lofibox::runtime::RuntimeCommandKind::RemoteStartActiveStream,
                payload,
                ::lofibox::runtime::CommandOrigin::Desktop}).applied) {
            state_.navigation.replaceStack({AppPage::MainMenu, AppPage::NowPlaying});
        }
        state_.pending_open_processed = true;
        return;
    }

    const auto services = appServices();
    if (services.libraryQueries().state() != LibraryIndexState::Ready && services.libraryQueries().state() != LibraryIndexState::Degraded) {
        return;
    }
    const std::filesystem::path requested{uri};
    std::error_code ec{};
    const auto absolute = std::filesystem::absolute(requested, ec);
    for (const auto& track : services.libraryQueries().model().tracks) {
        const auto track_absolute = std::filesystem::absolute(track.path, ec);
        if ((!ec && track_absolute == absolute) || track.path == requested) {
            if (startLibraryTrack(track.id)) {
                state_.navigation.replaceStack({AppPage::MainMenu, AppPage::NowPlaying});
            }
            state_.pending_open_processed = true;
            return;
        }
    }
    state_.pending_open_processed = true;
}

AppPage AppRuntimeContext::currentPage() const noexcept
{
    return state_.navigation.currentPage();
}

NavigationState& AppRuntimeContext::navigationState() noexcept
{
    return state_.navigation;
}

::lofibox::application::AppServiceRegistry AppRuntimeContext::appServices() noexcept
{
    return app_host_.registry();
}

::lofibox::application::AppServiceRegistry AppRuntimeContext::appServices() const noexcept
{
    return app_host_.registry();
}

::lofibox::runtime::RuntimeCommandResult AppRuntimeContext::submitRuntimeCommand(::lofibox::runtime::RuntimeCommand command)
{
    return runtime_client_.dispatch(command);
}

::lofibox::application::SourceProfileCommandService AppRuntimeContext::sourceProfileService() const noexcept
{
    return appServices().sourceProfiles();
}

::lofibox::application::RemoteBrowseQueryService AppRuntimeContext::remoteBrowseService() const noexcept
{
    return appServices().remoteBrowseQueries();
}

EqState& AppRuntimeContext::eqState() noexcept
{
    return state_.eq;
}

int& AppRuntimeContext::mainMenuIndex() noexcept
{
    return state_.main_menu_index;
}

void AppRuntimeContext::closeHelpForCommand() noexcept
{
    closeHelp();
}

std::chrono::steady_clock::time_point AppRuntimeContext::lastUpdate() const noexcept
{
    return state_.last_update;
}

void AppRuntimeContext::setLastUpdate(std::chrono::steady_clock::time_point value) noexcept
{
    state_.last_update = value;
}

const ui::UiAssets& AppRuntimeContext::assets() const noexcept
{
    return state_.ui_assets;
}

std::chrono::steady_clock::time_point AppRuntimeContext::bootStarted() const noexcept
{
    return state_.boot_started;
}

LibraryIndexState AppRuntimeContext::libraryState() const noexcept
{
    return appServices().libraryQueries().state();
}

void AppRuntimeContext::startLibraryLoading()
{
    appServices().libraryMutations().startLoading();
}

void AppRuntimeContext::refreshLibrary()
{
    (void)appServices().libraryMutations().refreshLibrary(state_.media_roots);
    refreshRemoteLibraryTracks();
}

void AppRuntimeContext::refreshRemoteLibraryTracks()
{
    for (auto& profile : state_.remote_profiles) {
        if (profile.base_url.empty()) {
            continue;
        }
        auto source_profiles = sourceProfileService();
        if (source_profiles.readiness(profile) != "READY") {
            continue;
        }

        auto library_result = remoteBrowseService().libraryTracks(profile, state_.remote_profiles.size(), kUnifiedRemoteLibraryTrackLimit);
        if (!library_result.session.available) {
            continue;
        }

        appServices().libraryMutations().mergeRemoteTracks(profile, library_result.playable_tracks);
    }
}

void AppRuntimeContext::showMainMenu()
{
    state_.navigation.replaceStack({AppPage::MainMenu});
}

StorageInfo AppRuntimeContext::storage() const
{
    return appServices().libraryQueries().model().storage;
}

bool AppRuntimeContext::networkConnected() const noexcept
{
    return state_.network.connected;
}

int AppRuntimeContext::mainMenuIndex() const noexcept
{
    return state_.main_menu_index;
}

const PlaybackSession& AppRuntimeContext::playbackSession() const noexcept
{
    return appServices().playbackStatus().session();
}

const EqState& AppRuntimeContext::eqState() const noexcept
{
    return state_.eq;
}

::lofibox::runtime::EqRuntimeSnapshot AppRuntimeContext::eqRuntimeSnapshot() const noexcept
{
    return runtime_client_.query(::lofibox::runtime::RuntimeQuery{
        ::lofibox::runtime::RuntimeQueryKind::EqSnapshot,
        ::lofibox::runtime::CommandOrigin::Gui}).eq;
}

ListSelection AppRuntimeContext::listSelection() const noexcept
{
    return state_.navigation.list_selection;
}

AppPage AppRuntimeContext::helpPage() const noexcept
{
    return state_.help_page;
}

const TrackRecord* AppRuntimeContext::findTrack(int id) const noexcept
{
    return appServices().libraryQueries().findTrack(id);
}

void AppRuntimeContext::pushPage(AppPage page)
{
    commandPushPage(*this, page);
}

void AppRuntimeContext::popPage()
{
    commandPopPage(*this);
}

void AppRuntimeContext::closeHelp() noexcept
{
    state_.help_open = false;
}

void AppRuntimeContext::toggleHelpForCurrentPage() noexcept
{
    const auto page = currentPage();
    if (page == AppPage::Boot) {
        return;
    }
    if (state_.help_open && state_.help_page == page) {
        state_.help_open = false;
        return;
    }
    state_.help_page = page;
    state_.help_open = true;
}

bool AppRuntimeContext::helpOpen() const noexcept
{
    return state_.help_open;
}

void AppRuntimeContext::playFromMenu()
{
    (void)submitRuntimeCommand(::lofibox::runtime::RuntimeCommand{
        ::lofibox::runtime::RuntimeCommandKind::PlaybackPlay,
        {},
        ::lofibox::runtime::CommandOrigin::Gui});
}

void AppRuntimeContext::pausePlayback()
{
    (void)submitRuntimeCommand(::lofibox::runtime::RuntimeCommand{
        ::lofibox::runtime::RuntimeCommandKind::PlaybackPause,
        {},
        ::lofibox::runtime::CommandOrigin::Gui});
}

void AppRuntimeContext::stepTrack(int delta)
{
    (void)submitRuntimeCommand(::lofibox::runtime::RuntimeCommand{
        ::lofibox::runtime::RuntimeCommandKind::QueueStep,
        ::lofibox::runtime::RuntimeCommandPayload::queueStep(delta),
        ::lofibox::runtime::CommandOrigin::Gui});
}

void AppRuntimeContext::cycleMainMenuPlaybackMode()
{
    commandCycleMainMenuPlaybackMode(*this);
}

void AppRuntimeContext::toggleRepeatAll()
{
    commandToggleRepeatAll(*this);
}

void AppRuntimeContext::toggleRepeatOne()
{
    commandToggleRepeatOne(*this);
}

void AppRuntimeContext::openSearchPage()
{
    refreshSearchResults();
    commandPushPage(*this, AppPage::Search);
}

void AppRuntimeContext::openLibraryPage()
{
    commandPushPage(*this, AppPage::MusicIndex);
}

void AppRuntimeContext::openQueuePage()
{
    commandPushPage(*this, AppPage::Queue);
}

void AppRuntimeContext::openSettingsPage()
{
    commandPushPage(*this, AppPage::Settings);
}

void AppRuntimeContext::showMainMenuPage()
{
    closeHelp();
    showMainMenu();
}

const ui::UiTheme& AppRuntimeContext::theme() const noexcept
{
    return theme_;
}

AppPageModel AppRuntimeContext::pageModel() const
{
    const auto page = currentPage();
    auto page_rows = currentPageRows();
    const auto services = appServices();
    auto library_rows = services.libraryQueries().rowsForPage(page);
    if (page == AppPage::MusicIndex) {
        library_rows = libraryIndexRows();
    }
    auto title_override = services.libraryQueries().titleOverrideForPage(page);
    if (page == AppPage::RemoteBrowse) {
        title_override = remoteBrowseTitleOverride();
    } else if (page == AppPage::RemoteProfileSettings) {
        if (const auto profile = selectedRemoteProfile()) {
            title_override = sourceProfileService().kindDisplayName(profile->kind);
        }
    } else if (page == AppPage::ServerDiagnostics || page == AppPage::StreamDetail) {
        if (const auto profile = selectedRemoteProfile()) {
            title_override = sourceProfileService().profileLabel(*profile);
        }
    }
    return buildAppPageModel(AppPageModelInput{
        page,
        title_override,
        library_rows,
        state_.settings,
        state_.network.connected,
        state_.metadata_service.display_name,
        buildSourceManagerRows(state_.remote_profiles, remote::RemoteSourceRegistry{}.manifests()),
        page_rows.empty() ? std::optional<std::vector<std::pair<std::string, std::string>>>{} : std::make_optional(std::move(page_rows))});
}

void AppRuntimeContext::moveMainMenuSelection(int delta)
{
    commandMoveMainMenuSelection(*this, delta);
}

void AppRuntimeContext::resetMainMenuSelection() noexcept
{
    commandResetMainMenuSelection(*this);
}

void AppRuntimeContext::moveSelection(int delta)
{
    commandMoveSelection(*this, delta);
}

void AppRuntimeContext::moveSelectionPage(int delta_pages)
{
    commandMoveSelectionPage(*this, delta_pages);
}

bool AppRuntimeContext::isBrowseListPage() const noexcept
{
    return lofibox::app::isBrowseListPage(currentPage());
}

bool AppRuntimeContext::nowPlayingConfirmBlocked() const
{
    return clock::now() < state_.now_playing_confirm_blocked_until;
}

void AppRuntimeContext::confirmMainMenu()
{
    commandConfirmMainMenu(*this);
}

bool AppRuntimeContext::startLibraryTrack(int track_id)
{
    if (const auto* track = appServices().libraryQueries().findTrack(track_id); track != nullptr && track->remote) {
        const auto payload = remoteLibraryTrackPayload(*track);
        if (payload.get<::lofibox::runtime::RemotePlayResolvedLibraryTrackPayload>() == nullptr) {
            return false;
        }
        const auto result = submitRuntimeCommand(::lofibox::runtime::RuntimeCommand{
            ::lofibox::runtime::RuntimeCommandKind::RemoteStartActiveStream,
            payload,
            ::lofibox::runtime::CommandOrigin::Gui});
        return result.applied;
    }
    const auto result = submitRuntimeCommand(::lofibox::runtime::RuntimeCommand{
        ::lofibox::runtime::RuntimeCommandKind::PlaybackStartTrack,
        ::lofibox::runtime::RuntimeCommandPayload::startTrack(track_id),
        ::lofibox::runtime::CommandOrigin::Gui});
    return result.applied;
}

void AppRuntimeContext::toggleShuffle()
{
    (void)submitRuntimeCommand(::lofibox::runtime::RuntimeCommand{
        ::lofibox::runtime::RuntimeCommandKind::PlaybackToggleShuffle,
        {},
        ::lofibox::runtime::CommandOrigin::Gui});
}

void AppRuntimeContext::cycleRepeatMode()
{
    (void)submitRuntimeCommand(::lofibox::runtime::RuntimeCommand{
        ::lofibox::runtime::RuntimeCommandKind::PlaybackCycleRepeat,
        {},
        ::lofibox::runtime::CommandOrigin::Gui});
}

void AppRuntimeContext::togglePlayPause()
{
    (void)submitRuntimeCommand(::lofibox::runtime::RuntimeCommand{
        ::lofibox::runtime::RuntimeCommandKind::PlaybackToggle,
        {},
        ::lofibox::runtime::CommandOrigin::Gui});
}

void AppRuntimeContext::moveEqualizerSelection(int delta)
{
    commandMoveEqualizerSelection(*this, delta);
}

void AppRuntimeContext::adjustSelectedEqualizerBand(int delta)
{
    commandAdjustSelectedEqualizerBand(*this, delta);
}

void AppRuntimeContext::cycleEqualizerPreset(int delta)
{
    commandCycleEqualizerPreset(*this, delta);
}

void AppRuntimeContext::cycleSongSortModeAndClamp()
{
    commandCycleSongSortModeAndClamp(*this);
}

void AppRuntimeContext::confirmListPage()
{
    commandConfirmListPage(*this);
}

std::vector<std::pair<std::string, std::string>> AppRuntimeContext::currentPageRows() const
{
    switch (currentPage()) {
    case AppPage::RemoteBrowse:
        return remoteBrowseRows();
    case AppPage::ServerDiagnostics:
        return serverDiagnosticsRows();
    case AppPage::StreamDetail:
        return streamDetailRows();
    case AppPage::Queue:
        return queueRows();
    case AppPage::Search:
        return searchRows();
    case AppPage::RemoteSetup:
        return remoteSetupRows();
    case AppPage::RemoteProfileSettings:
        return remoteProfileSettingsRows();
    case AppPage::RemoteFieldEditor:
        return remoteFieldEditorRows();
    case AppPage::PlaylistEditor:
        return {{"CREATE PLAYLIST", "OK"}, {"ADD CURRENT", "INS"}, {"REMOVE ITEM", "DEL"}, {"REORDER", "UP/DOWN"}, {"SAVE", "OK"}};
    default:
        return {};
    }
}

std::vector<std::pair<std::string, std::string>> AppRuntimeContext::libraryIndexRows() const
{
    auto rows = appServices().libraryQueries().rowsForPage(AppPage::MusicIndex).value_or(std::vector<std::pair<std::string, std::string>>{});
    for (const auto& profile : state_.remote_profiles) {
        int track_count = 0;
        for (const auto& track : appServices().libraryQueries().model().tracks) {
            if (track.remote && track.remote_profile_id == profile.id) {
                ++track_count;
            }
        }
        rows.emplace_back(sourceProfileService().profileLabel(profile), std::to_string(track_count));
    }
    return rows;
}

int AppRuntimeContext::libraryRemoteRowBase() const
{
    const auto rows = appServices().libraryQueries().rowsForPage(AppPage::MusicIndex).value_or(std::vector<std::pair<std::string, std::string>>{});
    return static_cast<int>(rows.size());
}

std::vector<std::pair<std::string, std::string>> AppRuntimeContext::remoteBrowseRows() const
{
    const auto browse_secondary = [](const RemoteCatalogNode& node) {
        if (node.playable) {
            if (!node.artist.empty() && !node.album.empty()) {
                return node.artist + " - " + node.album;
            }
            if (!node.artist.empty()) {
                return node.artist;
            }
            if (!node.album.empty()) {
                return node.album;
            }
            if (!node.subtitle.empty()) {
                return node.subtitle;
            }
            return std::string{"TRACK"};
        }
        return node.subtitle;
    };

    std::vector<std::pair<std::string, std::string>> rows{};
    for (const auto& node : state_.remote_browse_nodes) {
        rows.emplace_back(node.title.empty() ? node.id : node.title, browse_secondary(node));
    }
    return rows;
}

std::optional<std::string> AppRuntimeContext::remoteBrowseTitleOverride() const
{
    if (state_.selected_remote_parent.kind != RemoteCatalogNodeKind::Root
        || !state_.selected_remote_parent.id.empty()
        || !state_.selected_remote_parent.title.empty()) {
        if (!state_.selected_remote_parent.title.empty()) {
            return state_.selected_remote_parent.title;
        }
        if (!state_.selected_remote_parent.id.empty()) {
            return state_.selected_remote_parent.id;
        }
    }
    if (const auto profile = selectedRemoteProfile()) {
        return sourceProfileService().profileLabel(*profile);
    }
    return std::nullopt;
}

std::optional<RemoteServerProfile> AppRuntimeContext::selectedRemoteProfile() const
{
    if (!state_.selected_remote_profile_index || *state_.selected_remote_profile_index >= state_.remote_profiles.size()) {
        return state_.selected_transient_remote_profile;
    }
    return state_.remote_profiles[*state_.selected_remote_profile_index];
}

RemoteServerProfile* AppRuntimeContext::selectedMutableRemoteProfile() noexcept
{
    if (!state_.selected_remote_profile_index || *state_.selected_remote_profile_index >= state_.remote_profiles.size()) {
        return nullptr;
    }
    return &state_.remote_profiles[*state_.selected_remote_profile_index];
}

std::vector<std::pair<std::string, std::string>> AppRuntimeContext::serverDiagnosticsRows() const
{
    const auto diagnostics = remoteBrowseService().sourceDiagnostics(selectedRemoteProfile(), state_.selected_remote_session);
    return {
        {"SOURCE", diagnostics.source_label},
        {"TYPE", diagnostics.kind_id.empty() ? "UNKNOWN" : diagnostics.kind_id},
        {"CONNECTION", diagnostics.connection_status},
        {"MESSAGE", diagnostics.message.empty() ? "-" : diagnostics.message},
        {"USER", diagnostics.user.empty() ? "-" : diagnostics.user},
        {"CREDENTIAL", diagnostics.credential_status.empty() ? "NONE" : diagnostics.credential_status},
        {"TLS", diagnostics.tls_status.empty() ? "UNKNOWN" : diagnostics.tls_status},
        {"PERMISSION", diagnostics.permission.empty() ? "UNKNOWN" : diagnostics.permission},
        {"TOKEN", diagnostics.token_status.empty() ? "NONE" : diagnostics.token_status},
    };
}

std::vector<std::pair<std::string, std::string>> AppRuntimeContext::streamDetailRows() const
{
    const auto diagnostics = remoteBrowseService().streamDiagnostics(selectedRemoteProfile(), state_.selected_remote_stream);
    if (!diagnostics.resolved) {
        return {{"STREAM", "NOT RESOLVED"}, {"ENTER", "RETRY"}};
    }
    return {
        {"SOURCE", diagnostics.source_name.empty() ? "REMOTE" : diagnostics.source_name},
        {"URL", diagnostics.redacted_url.empty() ? "REDACTED" : diagnostics.redacted_url},
        {"CONNECTION", diagnostics.connection_status.empty() ? "UNKNOWN" : diagnostics.connection_status},
        {"BUFFER", diagnostics.buffer_state.empty() ? "UNKNOWN" : diagnostics.buffer_state},
        {"MIN BUFFER", std::to_string(diagnostics.minimum_playable_seconds) + "S"},
        {"RECOVERY", diagnostics.recovery_action.empty() ? "NONE" : diagnostics.recovery_action},
        {"QUALITY", qualityLabel(diagnostics.quality_preference)},
        {"BITRATE", diagnostics.bitrate_kbps > 0 ? std::to_string(diagnostics.bitrate_kbps) + "K" : "UNKNOWN"},
        {"CODEC", diagnostics.codec.empty() ? "UNKNOWN" : diagnostics.codec},
        {"SAMPLE RATE", diagnostics.sample_rate_hz > 0 ? std::to_string(diagnostics.sample_rate_hz) + "HZ" : "UNKNOWN"},
        {"CHANNELS", diagnostics.channel_count > 0 ? std::to_string(diagnostics.channel_count) : "UNKNOWN"},
        {"LIVE", diagnostics.live ? "YES" : "NO"},
        {"ENTER", "PLAY"},
    };
}

std::vector<std::pair<std::string, std::string>> AppRuntimeContext::queueRows() const
{
    const auto runtime_snapshot = runtime_client_.snapshot();
    const auto& playback = runtime_snapshot.playback;
    const auto& queue = runtime_snapshot.queue;
    std::vector<std::pair<std::string, std::string>> rows{};
    if (playback.current_track_id) {
        if (const auto* track = appServices().libraryQueries().findTrack(*playback.current_track_id)) {
            rows.emplace_back(track->title, track->remote && !track->source_label.empty() ? track->source_label : "LOCAL");
        }
    } else if (!playback.title.empty()) {
        rows.emplace_back(playback.title, playback.source_label.empty() ? "STREAM" : playback.source_label);
    }
    if (rows.empty()) {
        rows.emplace_back("QUEUE EMPTY", "IDLE");
    }
    rows.emplace_back("MODE", queue.shuffle_enabled ? "SHUFFLE" : (queue.repeat_one ? "REPEAT ONE" : (queue.repeat_all ? "REPEAT ALL" : "ORDER")));
    return rows;
}

std::vector<std::pair<std::string, std::string>> AppRuntimeContext::searchRows() const
{
    std::vector<std::pair<std::string, std::string>> rows{};
    rows.emplace_back("QUERY", state_.search_query.empty() ? "TYPE..." : state_.search_query);
    if (state_.search_query.empty()) {
        rows.emplace_back("LOCAL + REMOTE", "WAITING");
        return rows;
    }

    for (const auto& item : state_.search_results) {
        rows.emplace_back(item.title.empty() ? item.id : item.title, item.source.source_label.empty() ? "LOCAL" : item.source.source_label);
    }

    for (const auto& degraded : state_.search_degraded_sources) {
        rows.emplace_back(degraded, "TIMEOUT");
    }

    if (rows.size() == 1) {
        rows.emplace_back("NO RESULTS", "EMPTY");
    }
    return rows;
}

std::vector<std::pair<std::string, std::string>> AppRuntimeContext::remoteSetupRows() const
{
    std::vector<std::pair<std::string, std::string>> rows{};
    for (const auto& manifest : remote::RemoteSourceRegistry{}.manifests()) {
        const auto profile = std::find_if(state_.remote_profiles.begin(), state_.remote_profiles.end(), [&manifest](const RemoteServerProfile& candidate) {
            return candidate.kind == manifest.kind;
        });
        rows.emplace_back(
            manifest.display_name,
            profile == state_.remote_profiles.end() ? std::string{"ADD"} : sourceProfileService().readiness(*profile));
    }
    return rows;
}

std::vector<std::pair<std::string, std::string>> AppRuntimeContext::remoteProfileSettingsRows() const
{
    const auto profile = selectedRemoteProfile();
    if (!profile) {
        return {
            {"PROFILE", "ADD NEW"},
            {"TYPE", "JELLYFIN"},
            {"ADDRESS", "MISSING"},
            {"USER", "MISSING"},
            {"PASSWORD", "EMPTY"},
            {"SAVE", "ENTER"},
        };
    }

    return {
        {"PROFILE", sourceProfileService().profileLabel(*profile)},
        {"TYPE", sourceProfileService().kindDisplayName(profile->kind)},
        {"LABEL", profile->name.empty() ? "MISSING" : profile->name},
        {"ADDRESS", profile->base_url.empty() ? "MISSING" : profile->base_url},
        {"USER", sourceProfileService().usernameLabel(*profile)},
        {"PASSWORD", sourceProfileService().credentialValueLabel(profile->kind, profile->password)},
        {"API TOKEN", sourceProfileService().credentialValueLabel(profile->kind, profile->api_token)},
        {"TLS VERIFY", profile->tls_policy.verify_peer ? "ON" : "OFF"},
        {"SELF SIGNED", profile->tls_policy.allow_self_signed ? "ALLOW" : "BLOCK"},
        {"PERMISSION", sourceProfileService().permissionLabel(profile->kind)},
        {"TEST", state_.remote_profile_status.empty() ? sourceProfileService().readiness(*profile) : state_.remote_profile_status},
        {"SAVE", "PROFILE"},
    };
}

std::vector<std::pair<std::string, std::string>> AppRuntimeContext::remoteFieldEditorRows() const
{
    const bool secret = state_.remote_profile_edit_field == kRemoteProfileRowPassword
        || state_.remote_profile_edit_field == kRemoteProfileRowToken;
    return {
        {"FIELD", remoteEditFieldName(state_.remote_profile_edit_field)},
        {"VALUE", secret ? hiddenSecretLabel(state_.remote_profile_edit_buffer) : (state_.remote_profile_edit_buffer.empty() ? "EMPTY" : state_.remote_profile_edit_buffer)},
        {"ENTER", "SAVE"},
        {"BACK", "CANCEL"},
    };
}

void AppRuntimeContext::openRemoteProfile(std::size_t profile_index)
{
    if (profile_index >= state_.remote_profiles.size()) {
        return;
    }
    state_.selected_remote_profile_index = profile_index;
    state_.selected_transient_remote_profile.reset();
    state_.selected_remote_kind = state_.remote_profiles[profile_index].kind;
    loadRemoteRoot();
}

void AppRuntimeContext::loadRemoteRoot()
{
    state_.selected_remote_parent = RemoteCatalogNode{};
    auto* profile = selectedMutableRemoteProfile();
    if (profile == nullptr) {
        state_.remote_browse_nodes = {};
        return;
    }
    const auto result = remoteBrowseService().browseRoot(*profile, state_.remote_profiles.size(), 100);
    state_.selected_remote_session = result.session;
    state_.remote_browse_nodes = result.nodes;
}

bool AppRuntimeContext::persistRemoteProfiles()
{
    const bool saved = sourceProfileService().persistProfiles(state_.remote_profiles);
    state_.remote_profile_status = saved ? "SAVED" : "SAVE FAIL";
    return saved;
}

void AppRuntimeContext::openRemoteSetup()
{
    state_.remote_profiles = sourceProfileService().loadProfiles();
    state_.selected_transient_remote_profile.reset();
    state_.remote_profile_status.clear();
    state_.navigation.list_selection.selected = 0;
    state_.navigation.list_selection.scroll = 0;
    commandPushPage(*this, AppPage::RemoteSetup);
}

void AppRuntimeContext::openRemoteProfileSettings(std::size_t profile_index, int focus_row)
{
    if (profile_index >= state_.remote_profiles.size()) {
        openNewRemoteProfileSettings(RemoteServerKind::Jellyfin);
        return;
    }
    state_.selected_remote_profile_index = profile_index;
    state_.selected_transient_remote_profile.reset();
    state_.selected_remote_kind = state_.remote_profiles[profile_index].kind;
    state_.remote_profile_status = sourceProfileService().readiness(state_.remote_profiles[profile_index]);
    state_.navigation.list_selection.selected = std::max(0, focus_row);
    state_.navigation.list_selection.scroll = 0;
    commandPushPage(*this, AppPage::RemoteProfileSettings);
}

void AppRuntimeContext::openNewRemoteProfileSettings(RemoteServerKind kind)
{
    state_.remote_profiles.push_back(sourceProfileService().createProfile(kind, state_.remote_profiles.size()));
    openRemoteProfileSettings(state_.remote_profiles.size() - 1U, kRemoteProfileRowKind);
}

void AppRuntimeContext::beginRemoteProfileFieldEdit(int field)
{
    auto* profile = selectedMutableRemoteProfile();
    if (profile == nullptr) {
        openNewRemoteProfileSettings(RemoteServerKind::Jellyfin);
        profile = selectedMutableRemoteProfile();
    }
    if (profile == nullptr) {
        return;
    }

    state_.remote_profile_edit_field = field;
    switch (field) {
    case kRemoteProfileRowName:
        state_.remote_profile_edit_buffer = profile->name;
        break;
    case kRemoteProfileRowAddress:
        state_.remote_profile_edit_buffer = profile->base_url;
        break;
    case kRemoteProfileRowUsername:
        state_.remote_profile_edit_buffer = profile->username;
        break;
    case kRemoteProfileRowPassword:
    case kRemoteProfileRowToken:
        state_.remote_profile_edit_buffer.clear();
        break;
    default:
        return;
    }
    commandPushPage(*this, AppPage::RemoteFieldEditor);
}

bool AppRuntimeContext::handleSettingsRemoteConfirm(int selected)
{
    (void)selected;
    openRemoteSetup();
    return true;
}

bool AppRuntimeContext::handleRemoteSetupConfirm(int selected)
{
    const auto manifests = remote::RemoteSourceRegistry{}.manifests();
    if (selected < 0 || selected >= static_cast<int>(manifests.size())) {
        return false;
    }

    const auto kind = manifests[static_cast<std::size_t>(selected)].kind;
    if (const auto index = sourceProfileService().findProfileByKind(state_.remote_profiles, kind)) {
        openRemoteProfileSettings(*index, kRemoteProfileRowAddress);
    } else {
        openNewRemoteProfileSettings(kind);
    }
    return true;
}

bool AppRuntimeContext::handleLibraryRemoteConfirm(int selected)
{
    const int remote_base = libraryRemoteRowBase();
    if (selected < remote_base) {
        return false;
    }
    const auto profile_index = static_cast<std::size_t>(selected - remote_base);
    if (profile_index >= state_.remote_profiles.size()) {
        return false;
    }
    openRemoteProfile(profile_index);
    commandPushPage(*this, state_.selected_remote_session.available ? AppPage::RemoteBrowse : AppPage::ServerDiagnostics);
    return true;
}

bool AppRuntimeContext::handleSourceManagerConfirm(int selected)
{
    const auto manifests = remote::RemoteSourceRegistry{}.manifests();
    if (selected == 0) {
        commandPushPage(*this, AppPage::MusicIndex);
        return true;
    }

    const int profile_base = 3 + static_cast<int>(manifests.size());
    if (selected >= profile_base) {
        const auto profile_index = static_cast<std::size_t>(selected - profile_base);
        openRemoteProfile(profile_index);
        commandPushPage(*this, AppPage::RemoteBrowse);
        return true;
    }

    if (selected == 1 || selected == 2) {
        const RemoteServerKind wanted = selected == 1 ? RemoteServerKind::DirectUrl : RemoteServerKind::InternetRadio;
        if (const auto index = sourceProfileService().findProfileByKind(state_.remote_profiles, wanted)) {
            openRemoteProfileSettings(*index, kRemoteProfileRowAddress);
        } else {
            openNewRemoteProfileSettings(wanted);
        }
        return true;
    }

    const int manifest_index = selected - 3;
    if (manifest_index >= 0 && manifest_index < static_cast<int>(manifests.size())) {
        openNewRemoteProfileSettings(manifests[static_cast<std::size_t>(manifest_index)].kind);
        return true;
    }
    return false;
}

bool AppRuntimeContext::handleRemoteProfileSettingsConfirm(int selected)
{
    auto* profile = selectedMutableRemoteProfile();
    if (profile == nullptr) {
        openNewRemoteProfileSettings(RemoteServerKind::Jellyfin);
        return true;
    }

    switch (selected) {
    case kRemoteProfileRowProfile:
        if (!state_.remote_profiles.empty()) {
            const auto current = state_.selected_remote_profile_index.value_or(0U);
            const auto next = (current + 1U) % state_.remote_profiles.size();
            openRemoteProfileSettings(next, kRemoteProfileRowProfile);
        }
        return true;
    case kRemoteProfileRowKind:
        openRemoteSetup();
        return true;
    case kRemoteProfileRowName:
    case kRemoteProfileRowAddress:
    case kRemoteProfileRowUsername:
    case kRemoteProfileRowPassword:
    case kRemoteProfileRowToken:
        beginRemoteProfileFieldEdit(selected);
        return true;
    case kRemoteProfileRowTlsVerify:
        state_.remote_profile_status = sourceProfileService().toggleTlsVerify(*profile, state_.remote_profiles) ? "SAVED" : "SAVE FAIL";
        return true;
    case kRemoteProfileRowSelfSigned:
        state_.remote_profile_status = sourceProfileService().toggleSelfSigned(*profile, state_.remote_profiles) ? "SAVED" : "SAVE FAIL";
        return true;
    case kRemoteProfileRowPermission:
        state_.remote_profile_status = sourceProfileService().permissionLabel(profile->kind);
        return true;
    case kRemoteProfileRowTest:
        {
            const auto result = sourceProfileService().probe(*profile, state_.remote_profiles.size());
            state_.selected_remote_session = result.session;
            state_.remote_profile_status = result.command.accepted ? "ONLINE" : "FAILED";
        }
        return true;
    case kRemoteProfileRowSave:
        (void)persistRemoteProfiles();
        return true;
    default:
        return false;
    }
}

bool AppRuntimeContext::handleRemoteBrowseConfirm(int selected)
{
    if (selected < 0 || selected >= static_cast<int>(state_.remote_browse_nodes.size())) {
        return false;
    }
    const auto profile = selectedRemoteProfile();
    if (!profile) {
        return false;
    }

    const auto node = state_.remote_browse_nodes[static_cast<std::size_t>(selected)];
    state_.selected_remote_node = node;
    remoteBrowseService().rememberRecentBrowse(*profile, node);
    if (node.playable) {
        const auto track = remoteBrowseService().trackFromNode(*profile, node);
        remoteBrowseService().rememberTrackFacts(*profile, track);
        std::vector<RemoteTrack> visible_tracks = remoteBrowseService().tracksFromPlayableNodes(*profile, state_.remote_browse_nodes);
        if (visible_tracks.empty()) {
            visible_tracks.push_back(track);
        }
        appServices().libraryMutations().mergeRemoteTracks(*profile, visible_tracks);

        std::vector<int> ids{};
        ids.reserve(visible_tracks.size());
        int selected_track_id = 0;
        for (const auto& visible_track : visible_tracks) {
            for (const auto& library_track : appServices().libraryQueries().model().tracks) {
                if (library_track.remote
                    && library_track.remote_profile_id == profile->id
                    && library_track.remote_track_id == visible_track.id) {
                    ids.push_back(library_track.id);
                    if (visible_track.id == track.id) {
                        selected_track_id = library_track.id;
                    }
                    break;
                }
            }
        }
        if (!ids.empty()) {
            appServices().libraryMutations().setSongsContextTrackIds(
                state_.selected_remote_parent.title.empty() ? std::string{"SONGS"} : state_.selected_remote_parent.title,
                ids);
        }
        if (selected_track_id != 0 && startLibraryTrack(selected_track_id)) {
            commandPushPage(*this, AppPage::NowPlaying);
        }
        return true;
    }

    if (node.browsable) {
        state_.selected_remote_parent = node;
        const auto result = remoteBrowseService().browseChild(
            state_.remote_profiles[*state_.selected_remote_profile_index],
            state_.selected_remote_session,
            node,
            100);
        state_.selected_remote_session = result.session;
        state_.remote_browse_nodes = result.nodes;
        state_.navigation.list_selection.selected = 0;
        state_.navigation.list_selection.scroll = 0;
        return true;
    }
    return false;
}

bool AppRuntimeContext::handleStreamDetailConfirm()
{
    const auto profile = selectedRemoteProfile();
    if (!profile) {
        return false;
    }
    if (!state_.selected_remote_stream) {
        if (!state_.selected_remote_node) {
            return false;
        }
        auto* mutable_profile = selectedMutableRemoteProfile();
        if (mutable_profile == nullptr) {
            return false;
        }
        const auto track = remoteBrowseService().trackFromNode(*mutable_profile, *state_.selected_remote_node);
        state_.selected_remote_stream = remoteBrowseService().resolve(*mutable_profile, state_.selected_remote_session, track).stream;
    }
    if (!state_.selected_remote_stream) {
        return false;
    }
    const auto payload = selectedRemoteStreamPayload();
    if (payload.get<::lofibox::runtime::RemotePlayResolvedStreamPayload>() == nullptr) {
        return false;
    }
    const auto result = submitRuntimeCommand(::lofibox::runtime::RuntimeCommand{
        ::lofibox::runtime::RuntimeCommandKind::RemoteStartActiveStream,
        payload,
        ::lofibox::runtime::CommandOrigin::Gui});
    return result.applied;
}

bool AppRuntimeContext::handleSearchConfirm(int selected)
{
    if (selected <= 0 || state_.search_query.empty()) {
        return false;
    }
    if (state_.search_results_query != state_.search_query) {
        refreshSearchResults();
    }
    const auto result_index = selected - 1;
    if (result_index < 0 || result_index >= static_cast<int>(state_.search_results.size())) {
        return false;
    }
    const auto& item = state_.search_results[static_cast<std::size_t>(result_index)];
    if (item.local_track_id) {
        return startLibraryTrack(*item.local_track_id);
    }

    const auto profile_it = std::find_if(state_.remote_profiles.begin(), state_.remote_profiles.end(), [&item](const RemoteServerProfile& profile) {
        return profile.id == item.source.source_id;
    });
    if (profile_it == state_.remote_profiles.end() || item.remote_track_id.empty()) {
        return false;
    }

    const auto profile_index = static_cast<std::size_t>(std::distance(state_.remote_profiles.begin(), profile_it));
    state_.selected_remote_profile_index = profile_index;
    state_.selected_remote_kind = profile_it->kind;
    state_.selected_remote_session = sourceProfileService().probe(state_.remote_profiles[profile_index], state_.remote_profiles.size()).session;
    if (!state_.selected_remote_session.available) {
        return false;
    }

    RemoteTrack track{};
    track.id = item.remote_track_id;
    track.title = item.title;
    track.artist = item.artist;
    track.album = item.album;
    track.duration_seconds = item.duration_seconds;
    track.source_id = profile_it->id;
    track.source_label = item.source.source_label.empty() ? sourceProfileService().profileLabel(*profile_it) : item.source.source_label;
    remoteBrowseService().rememberTrackFacts(state_.remote_profiles[profile_index], track);
    const auto resolve = remoteBrowseService().resolve(state_.remote_profiles[profile_index], state_.selected_remote_session, track);
    state_.selected_remote_stream = resolve.stream;
    track = resolve.track;
    state_.selected_remote_node = RemoteCatalogNode{
        RemoteCatalogNodeKind::Tracks,
        track.id,
        track.title,
        track.artist,
        true,
        false,
        track.artist,
        track.album,
        track.duration_seconds,
        track.album_id,
        track.source_id,
        track.source_label,
        track.artwork_key,
        track.artwork_url,
        track.lyrics_plain,
        track.lyrics_synced,
        track.lyrics_source,
        track.fingerprint};

    appServices().libraryMutations().mergeRemoteTracks(state_.remote_profiles[profile_index], std::vector<RemoteTrack>{track});
    int selected_track_id = 0;
    for (const auto& library_track : appServices().libraryQueries().model().tracks) {
        if (library_track.remote
            && library_track.remote_profile_id == profile_it->id
            && library_track.remote_track_id == track.id) {
            selected_track_id = library_track.id;
            break;
        }
    }
    if (selected_track_id == 0) {
        return false;
    }
    const auto source = !track.source_label.empty()
        ? track.source_label
        : sourceProfileService().profileLabel(*profile_it);
    const auto payload = ::lofibox::runtime::RuntimeCommandPayload::remoteResolvedLibraryTrack(
        selected_track_id,
        *profile_it,
        track,
        *state_.selected_remote_stream,
        source,
        sourceProfileService().keepsLocalFacts(profile_it->kind),
        remoteSessionSnapshotFor(*profile_it, *state_.selected_remote_stream, track, source));
    const auto result = submitRuntimeCommand(::lofibox::runtime::RuntimeCommand{
        ::lofibox::runtime::RuntimeCommandKind::RemoteStartActiveStream,
        payload,
        ::lofibox::runtime::CommandOrigin::Gui});
    return result.applied;
}

RemoteTrack AppRuntimeContext::remoteTrackFromLibraryTrack(const RemoteServerProfile& profile, const TrackRecord& track) const
{
    RemoteTrack remote_track{};
    remote_track.id = track.remote_track_id;
    remote_track.title = track.title;
    remote_track.artist = track.artist;
    remote_track.album = track.album;
    remote_track.duration_seconds = track.duration_seconds;
    remote_track.source_id = track.remote_profile_id.empty() ? profile.id : track.remote_profile_id;
    remote_track.source_label = track.source_label.empty()
        ? sourceProfileService().profileLabel(profile)
        : track.source_label;
    remote_track.artwork_key = track.artwork_key;
    remote_track.artwork_url = track.artwork_url;
    remote_track.lyrics_plain = track.lyrics_plain;
    remote_track.lyrics_synced = track.lyrics_synced;
    remote_track.lyrics_source = track.lyrics_source;
    remote_track.fingerprint = track.fingerprint;

    return remoteBrowseService().trackFromNode(
        profile,
        RemoteCatalogNode{
            RemoteCatalogNodeKind::Tracks,
            remote_track.id,
            remote_track.title,
            remote_track.artist,
            true,
            false,
            remote_track.artist,
            remote_track.album,
            remote_track.duration_seconds,
            remote_track.album_id,
            remote_track.source_id,
            remote_track.source_label,
            remote_track.artwork_key,
            remote_track.artwork_url,
            remote_track.lyrics_plain,
            remote_track.lyrics_synced,
            remote_track.lyrics_source,
            remote_track.fingerprint});
}

::lofibox::runtime::RuntimeCommandPayload AppRuntimeContext::remoteLibraryTrackPayload(const TrackRecord& track)
{
    if (!track.remote || track.remote_profile_id.empty() || track.remote_track_id.empty()) {
        return ::lofibox::runtime::RuntimeCommandPayload::empty();
    }

    const auto profile_it = std::find_if(state_.remote_profiles.begin(), state_.remote_profiles.end(), [&track](const RemoteServerProfile& profile) {
        return profile.id == track.remote_profile_id;
    });
    if (profile_it == state_.remote_profiles.end()) {
        return ::lofibox::runtime::RuntimeCommandPayload::empty();
    }

    const auto profile_index = static_cast<std::size_t>(std::distance(state_.remote_profiles.begin(), profile_it));
    state_.selected_remote_profile_index = profile_index;
    state_.selected_remote_kind = profile_it->kind;
    state_.selected_remote_session = sourceProfileService().probe(state_.remote_profiles[profile_index], state_.remote_profiles.size()).session;
    if (!state_.selected_remote_session.available) {
        return ::lofibox::runtime::RuntimeCommandPayload::empty();
    }

    auto remote_track = remoteTrackFromLibraryTrack(state_.remote_profiles[profile_index], track);
    if (remote_track.source_label.empty()) {
        remote_track.source_label = sourceProfileService().profileLabel(*profile_it);
    }
    remoteBrowseService().rememberTrackFacts(state_.remote_profiles[profile_index], remote_track);
    state_.selected_remote_node = RemoteCatalogNode{
        RemoteCatalogNodeKind::Tracks,
        remote_track.id,
        remote_track.title,
        remote_track.artist,
        true,
        false,
        remote_track.artist,
        remote_track.album,
        remote_track.duration_seconds,
        remote_track.album_id,
        remote_track.source_id,
        remote_track.source_label,
        remote_track.artwork_key,
        remote_track.artwork_url,
        remote_track.lyrics_plain,
        remote_track.lyrics_synced,
        remote_track.lyrics_source,
        remote_track.fingerprint};
    const auto resolve = remoteBrowseService().resolve(state_.remote_profiles[profile_index], state_.selected_remote_session, remote_track);
    state_.selected_remote_stream = resolve.stream;
    remote_track = resolve.track;
    if (!state_.selected_remote_stream) {
        return ::lofibox::runtime::RuntimeCommandPayload::empty();
    }

    const auto source = !remote_track.source_label.empty()
        ? remote_track.source_label
        : sourceProfileService().profileLabel(*profile_it);
    return ::lofibox::runtime::RuntimeCommandPayload::remoteResolvedLibraryTrack(
        track.id,
        *profile_it,
        remote_track,
        *state_.selected_remote_stream,
        source,
        sourceProfileService().keepsLocalFacts(profile_it->kind),
        remoteSessionSnapshotFor(*profile_it, *state_.selected_remote_stream, remote_track, source));
}

::lofibox::runtime::RuntimeCommandPayload AppRuntimeContext::selectedRemoteStreamPayload() const
{
    const auto profile = selectedRemoteProfile();
    if (!profile || !state_.selected_remote_stream) {
        return ::lofibox::runtime::RuntimeCommandPayload::empty();
    }

    const auto track = state_.selected_remote_node
        ? remoteBrowseService().trackFromNode(*profile, *state_.selected_remote_node)
        : RemoteTrack{};
    const auto source = sourceProfileService().profileLabel(*profile);
    return ::lofibox::runtime::RuntimeCommandPayload::remoteResolvedStream(
        *state_.selected_remote_stream,
        track,
        source,
        remoteSessionSnapshotFor(*profile, *state_.selected_remote_stream, track, source));
}

::lofibox::runtime::RemoteSessionSnapshot AppRuntimeContext::remoteSessionSnapshotFor(
    const RemoteServerProfile& profile,
    const ResolvedRemoteStream& stream,
    const RemoteTrack& track,
    std::string source) const
{
    ::lofibox::runtime::RemoteSessionSnapshot snapshot{};
    snapshot.profile_id = profile.id;
    snapshot.source_label = source.empty() ? sourceProfileService().profileLabel(profile) : std::move(source);
    snapshot.connection_status = state_.selected_remote_session.available ? "ONLINE" : "UNAVAILABLE";
    if (!state_.selected_remote_session.message.empty() && !state_.selected_remote_session.available) {
        snapshot.connection_status = state_.selected_remote_session.message;
    }
    const auto& diagnostics = stream.diagnostics;
    snapshot.stream_resolved = true;
    snapshot.redacted_url = diagnostics.resolved_url_redacted;
    snapshot.buffer_state = diagnostics.connected ? "READY" : "UNCONFIRMED";
    snapshot.recovery_action = diagnostics.connected ? "NONE" : "RECONNECT";
    snapshot.bitrate_kbps = diagnostics.bitrate_kbps;
    snapshot.codec = diagnostics.codec;
    snapshot.live = diagnostics.live;
    snapshot.seekable = stream.seekable && diagnostics.seekable;
    if (!diagnostics.connection_status.empty()) {
        snapshot.connection_status = diagnostics.connection_status;
    }
    if (!diagnostics.source_name.empty()) {
        snapshot.source_label = diagnostics.source_name;
    }
    if (!track.source_label.empty()) {
        snapshot.source_label = track.source_label;
    }
    return snapshot;
}

void AppRuntimeContext::refreshSearchResults()
{
    state_.search_results.clear();
    state_.search_degraded_sources.clear();
    state_.search_results_query = state_.search_query;
    if (state_.search_query.empty()) {
        state_.navigation.list_selection.selected = 0;
        state_.navigation.list_selection.scroll = 0;
        return;
    }

    std::unordered_set<std::string> seen{};
    const auto remember_item = [&](MediaItem item) {
        if (state_.search_results.size() >= 12U) {
            return;
        }
        const auto key = searchItemKey(item);
        if (!seen.insert(key).second) {
            return;
        }
        state_.search_results.push_back(std::move(item));
    };

    const auto local_result = appServices().libraryQueries().searchLocal(state_.search_query, 8);
    for (auto item : local_result) {
        remember_item(std::move(item));
    }

    for (auto& profile : state_.remote_profiles) {
        if (state_.search_results.size() >= 12U || profile.base_url.empty() || sourceProfileService().readiness(profile) != "READY") {
            continue;
        }
        const auto remote_result = remoteBrowseService().search(profile, state_.remote_profiles.size(), state_.search_query, 4);
        if (remote_result.degraded) {
            state_.search_degraded_sources.push_back(remote_result.source_label.empty() ? sourceProfileService().profileLabel(profile) : remote_result.source_label);
            continue;
        }
        for (const auto& track : remote_result.tracks) {
            remember_item(mediaItemFromRemoteTrack(profile, track));
        }
    }
}

void AppRuntimeContext::appendSearchText(std::string_view text)
{
    if (appendUtf8Bounded(state_.search_query, text, 64U)) {
        refreshSearchResults();
        if (!state_.search_results.empty() && state_.navigation.list_selection.selected == 0) {
            state_.navigation.list_selection.selected = 1;
        }
    }
}

void AppRuntimeContext::backspaceSearchQuery()
{
    if (popLastUtf8Codepoint(state_.search_query)) {
        refreshSearchResults();
    }
}

void AppRuntimeContext::appendRemoteProfileEditText(std::string_view text)
{
    (void)appendUtf8Bounded(state_.remote_profile_edit_buffer, text, 256U);
}

void AppRuntimeContext::backspaceRemoteProfileEdit()
{
    (void)popLastUtf8Codepoint(state_.remote_profile_edit_buffer);
}

void AppRuntimeContext::commitRemoteProfileEdit()
{
    auto* profile = selectedMutableRemoteProfile();
    if (profile == nullptr) {
        commandPopPage(*this);
        return;
    }

    switch (state_.remote_profile_edit_field) {
    case kRemoteProfileRowName:
        (void)sourceProfileService().updateTextField(*profile, ::lofibox::application::SourceProfileTextField::Label, state_.remote_profile_edit_buffer, state_.remote_profiles.size());
        break;
    case kRemoteProfileRowAddress:
        (void)sourceProfileService().updateTextField(*profile, ::lofibox::application::SourceProfileTextField::Address, state_.remote_profile_edit_buffer, state_.remote_profiles.size());
        break;
    case kRemoteProfileRowUsername:
        (void)sourceProfileService().updateTextField(*profile, ::lofibox::application::SourceProfileTextField::Username, state_.remote_profile_edit_buffer, state_.remote_profiles.size());
        break;
    case kRemoteProfileRowPassword:
        (void)appServices().credentials().setSecret(
            *profile,
            ::lofibox::application::CredentialSecretPatch{"", state_.remote_profile_edit_buffer, ""});
        break;
    case kRemoteProfileRowToken:
        (void)appServices().credentials().setSecret(
            *profile,
            ::lofibox::application::CredentialSecretPatch{"", "", state_.remote_profile_edit_buffer});
        break;
    default:
        break;
    }

    state_.remote_profile_edit_field = -1;
    state_.remote_profile_edit_buffer.clear();
    (void)persistRemoteProfiles();
    state_.remote_profile_status = sourceProfileService().readiness(*profile);
    commandPopPage(*this);
}

} // namespace lofibox::app
