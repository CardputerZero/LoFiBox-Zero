// SPDX-License-Identifier: GPL-3.0-or-later

#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <set>
#include <thread>
#include <chrono>

#include "app/library_controller.h"
#include "playback/playback_controller.h"
#include "app/runtime_services.h"
#include "cache/cache_manager.h"

namespace fs = std::filesystem;

namespace {

void touchFile(const fs::path& path)
{
    std::ofstream output(path.string(), std::ios::binary);
    output << "test";
}

class FinishingAudioBackend final : public lofibox::app::AudioPlaybackBackend {
public:
    [[nodiscard]] bool available() const override { return true; }
    [[nodiscard]] std::string displayName() const override { return "TEST"; }
    bool playFile(const fs::path& path, double) override
    {
        finished = false;
        ++play_count;
        last_path = path;
        return true;
    }
    void stop() override { finished = true; }
    void pause() override { paused = true; }
    void resume() override { paused = false; }
    [[nodiscard]] bool isPlaying() override { return !finished && !paused; }
    [[nodiscard]] bool isFinished() override { return finished; }

    bool finished{false};
    bool paused{false};
    int play_count{0};
    fs::path last_path{};
};

class FinishOnSecondPollBackend final : public lofibox::app::AudioPlaybackBackend {
public:
    [[nodiscard]] bool available() const override { return true; }
    [[nodiscard]] std::string displayName() const override { return "POLL-RACE-TEST"; }
    bool playFile(const fs::path& path, double) override
    {
        last_path = path;
        finished_checks = 0;
        ++play_count;
        return true;
    }
    void stop() override {}
    void pause() override { paused = true; }
    void resume() override { paused = false; }
    [[nodiscard]] bool isPlaying() override { return !paused; }
    [[nodiscard]] bool isFinished() override { return ++finished_checks >= 2; }

    bool paused{false};
    int finished_checks{0};
    int play_count{0};
    fs::path last_path{};
};

class FailedAudioBackend final : public lofibox::app::AudioPlaybackBackend {
public:
    [[nodiscard]] bool available() const override { return true; }
    [[nodiscard]] std::string displayName() const override { return "FAILED-TEST"; }
    bool playFile(const fs::path& path, double) override
    {
        last_path = path;
        state_value = lofibox::app::AudioPlaybackState::Failed;
        ++play_count;
        return true;
    }
    void stop() override { state_value = lofibox::app::AudioPlaybackState::Idle; }
    [[nodiscard]] bool isPlaying() override { return false; }
    [[nodiscard]] bool isFinished() override { return false; }
    [[nodiscard]] lofibox::app::AudioPlaybackState state() override { return state_value; }

    lofibox::app::AudioPlaybackState state_value{lofibox::app::AudioPlaybackState::Idle};
    int play_count{0};
    fs::path last_path{};
};

class RejectingAudioBackend final : public lofibox::app::AudioPlaybackBackend {
public:
    [[nodiscard]] bool available() const override { return true; }
    [[nodiscard]] std::string displayName() const override { return "REJECT-TEST"; }
    bool playFile(const fs::path& path, double) override
    {
        last_path = path;
        ++play_count;
        return false;
    }
    void stop() override {}
    [[nodiscard]] bool isPlaying() override { return false; }
    [[nodiscard]] bool isFinished() override { return false; }

    int play_count{0};
    fs::path last_path{};
};

class StartingThenPlayingBackend final : public lofibox::app::AudioPlaybackBackend {
public:
    [[nodiscard]] bool available() const override { return true; }
    [[nodiscard]] std::string displayName() const override { return "START-GATE-TEST"; }
    bool playFile(const fs::path& path, double) override
    {
        last_path = path;
        state_value = lofibox::app::AudioPlaybackState::Starting;
        return true;
    }
    void stop() override { state_value = lofibox::app::AudioPlaybackState::Finished; }
    [[nodiscard]] bool isPlaying() override { return state_value == lofibox::app::AudioPlaybackState::Playing; }
    [[nodiscard]] bool isFinished() override { return state_value == lofibox::app::AudioPlaybackState::Finished; }
    [[nodiscard]] lofibox::app::AudioPlaybackState state() override { return state_value; }
    [[nodiscard]] lofibox::app::AudioVisualizationFrame visualizationFrame() const override
    {
        lofibox::app::AudioVisualizationFrame frame{};
        if (state_value == lofibox::app::AudioPlaybackState::Playing) {
            frame.available = true;
            frame.bands[3] = 0.8f;
        }
        return frame;
    }

    lofibox::app::AudioPlaybackState state_value{lofibox::app::AudioPlaybackState::Idle};
    fs::path last_path{};
};

class PositionReportingAudioBackend final : public lofibox::app::AudioPlaybackBackend {
public:
    [[nodiscard]] bool available() const override { return true; }
    [[nodiscard]] std::string displayName() const override { return "POSITION-CLOCK-TEST"; }
    bool playFile(const fs::path& path, double start_seconds) override
    {
        last_path = path;
        state_value = lofibox::app::AudioPlaybackState::Playing;
        position_seconds = start_seconds;
        return true;
    }
    void stop() override { state_value = lofibox::app::AudioPlaybackState::Finished; }
    [[nodiscard]] bool isPlaying() override { return state_value == lofibox::app::AudioPlaybackState::Playing; }
    [[nodiscard]] bool isFinished() override { return state_value == lofibox::app::AudioPlaybackState::Finished; }
    [[nodiscard]] lofibox::app::AudioPlaybackState state() override { return state_value; }
    [[nodiscard]] std::optional<double> positionSeconds() const override { return position_seconds; }

    lofibox::app::AudioPlaybackState state_value{lofibox::app::AudioPlaybackState::Idle};
    double position_seconds{0.0};
    fs::path last_path{};
};

class RemoteAudioBackend final : public lofibox::app::AudioPlaybackBackend {
public:
    [[nodiscard]] bool available() const override { return true; }
    [[nodiscard]] std::string displayName() const override { return "REMOTE-AUDIO-TEST"; }
    bool playFile(const fs::path&, double) override { return false; }
    bool playUri(const std::string& uri, double) override
    {
        played_uri = uri;
        finished = false;
        return true;
    }
    void stop() override { finished = true; }
    [[nodiscard]] bool isPlaying() override { return !played_uri.empty() && !finished; }
    [[nodiscard]] bool isFinished() override { return finished; }

    std::string played_uri{};
    bool finished{false};
};

class RemoteIdentityProvider final : public lofibox::app::TrackIdentityProvider {
public:
    [[nodiscard]] bool available() const override { return true; }
    [[nodiscard]] std::string displayName() const override { return "REMOTE-IDENTITY-TEST"; }
    [[nodiscard]] lofibox::app::TrackIdentity resolve(const fs::path&, const lofibox::app::TrackMetadata&) const override
    {
        lofibox::app::TrackIdentity identity{};
        identity.found = true;
        identity.source = "MUSICBRAINZ";
        identity.confidence = 0.92;
        identity.metadata.title = "Governed Song";
        identity.metadata.artist = "Governed Artist";
        identity.metadata.album = "Governed Album";
        identity.metadata.duration_seconds = 211;
        identity.fingerprint = "remote-fingerprint-123";
        return identity;
    }
};

class RemoteLyricsProvider final : public lofibox::app::LyricsProvider {
public:
    [[nodiscard]] bool available() const override { return true; }
    [[nodiscard]] std::string displayName() const override { return "REMOTE-LYRICS-TEST"; }
    [[nodiscard]] lofibox::app::TrackLyrics fetch(const fs::path&, const lofibox::app::TrackMetadata& metadata) const override
    {
        lofibox::app::TrackLyrics lyrics{};
        if (metadata.title && *metadata.title == "Governed Song") {
            lyrics.plain = "governed remote lyric";
            lyrics.source = "LRCLIB";
        }
        return lyrics;
    }
};

class FingerprintOnlyRemoteIdentityProvider final : public lofibox::app::TrackIdentityProvider {
public:
    [[nodiscard]] bool available() const override { return true; }
    [[nodiscard]] std::string displayName() const override { return "REMOTE-FINGERPRINT-ONLY-TEST"; }
    [[nodiscard]] lofibox::app::TrackIdentity resolve(const fs::path&, const lofibox::app::TrackMetadata&) const override
    {
        lofibox::app::TrackIdentity identity{};
        identity.fingerprint = "fingerprint-without-online-identity";
        return identity;
    }
    [[nodiscard]] lofibox::app::TrackIdentity resolveRemoteStream(
        std::string_view,
        std::string_view,
        const fs::path&,
        const lofibox::app::TrackMetadata&) const override
    {
        lofibox::app::TrackIdentity identity{};
        identity.fingerprint = "fingerprint-without-online-identity";
        return identity;
    }
};

} // namespace

int main()
{
    const fs::path root = fs::temp_directory_path() / "lofibox_zero_playback_controller_smoke";
    std::error_code ec{};
    fs::remove_all(root, ec);
    fs::create_directories(root / "Artist" / "Album");
    touchFile(root / "Artist" / "Album" / "alpha.mp3");
    touchFile(root / "Artist" / "Album" / "beta.mp3");
    touchFile(root / "Artist" / "Album" / "gamma.mp3");

    auto services = lofibox::app::withNullRuntimeServices();
    auto backend = std::make_shared<FinishingAudioBackend>();
    services.playback.audio_backend = backend;
    lofibox::app::LibraryController library{};
    library.startLoading();
    library.refreshLibrary({root}, *services.metadata.metadata_provider);
    library.setSongsContextAll();

    const auto ids = library.trackIdsForCurrentSongs();
    if (ids.size() != 3) {
        std::cerr << "Expected three tracks in playback source context.\n";
        return 1;
    }

    auto rejecting_services = lofibox::app::withNullRuntimeServices();
    auto rejecting_backend = std::make_shared<RejectingAudioBackend>();
    rejecting_services.playback.audio_backend = rejecting_backend;
    lofibox::app::PlaybackController rejecting_playback{};
    rejecting_playback.setServices(rejecting_services);
    if (rejecting_playback.startTrack(library, ids.front())) {
        std::cerr << "Expected playback controller to reject a track when the backend refuses to start.\n";
        return 1;
    }
    if (rejecting_playback.session().status == lofibox::app::PlaybackStatus::Playing || rejecting_playback.session().audio_active) {
        std::cerr << "Expected backend start refusal not to be projected as active playback.\n";
        return 1;
    }
    rejecting_playback.togglePlayPause();
    if (rejecting_playback.session().status == lofibox::app::PlaybackStatus::Playing || rejecting_playback.session().audio_active) {
        std::cerr << "Expected toggle after a rejected backend start not to create fake playback.\n";
        return 1;
    }

    lofibox::app::PlaybackController playback{};
    playback.setServices(services);
    if (!playback.startTrack(library, ids.front())) {
        std::cerr << "Expected playback controller to accept a valid track id.\n";
        return 1;
    }

    if (playback.session().status != lofibox::app::PlaybackStatus::Playing || !playback.session().current_track_id) {
        std::cerr << "Expected playback session to enter Playing state with a current track.\n";
        return 1;
    }

    playback.update(2.5, library);
    if (playback.session().elapsed_seconds < 2.0) {
        std::cerr << "Expected playback update to advance elapsed time while playing.\n";
        return 1;
    }
    if (playback.session().visualization_frame.available) {
        std::cerr << "Expected playback runtime not to synthesize fake visualization without backend audio samples.\n";
        return 1;
    }

    playback.togglePlayPause();
    if (playback.session().status != lofibox::app::PlaybackStatus::Paused) {
        std::cerr << "Expected toggle to pause playback.\n";
        return 1;
    }
    if (!backend->paused || backend->isPlaying()) {
        std::cerr << "Expected toggle pause to pause the audio backend.\n";
        return 1;
    }
    const double paused_elapsed = playback.session().elapsed_seconds;
    playback.update(3.0, library);
    if (playback.session().elapsed_seconds != paused_elapsed) {
        std::cerr << "Expected paused playback not to advance elapsed time.\n";
        return 1;
    }
    playback.togglePlayPause();
    if (playback.session().status != lofibox::app::PlaybackStatus::Playing) {
        std::cerr << "Expected toggle to resume playback.\n";
        return 1;
    }
    if (backend->paused || !backend->isPlaying()) {
        std::cerr << "Expected toggle resume to resume the audio backend.\n";
        return 1;
    }

    const auto first_track_id = *playback.session().current_track_id;
    (void)playback.stepQueue(library, 1);
    if (!playback.session().current_track_id || *playback.session().current_track_id == first_track_id) {
        std::cerr << "Expected queue step to move to the next track.\n";
        return 1;
    }

    playback.setRepeatOne(true);
    const int repeat_one_track_id = *playback.session().current_track_id;
    (void)playback.stepQueue(library, 1);
    if (!playback.session().current_track_id || *playback.session().current_track_id == repeat_one_track_id) {
        std::cerr << "Expected manual next to work even in repeat-one mode.\n";
        return 1;
    }

    playback.setRepeatOne(false);
    if (!playback.startTrack(library, ids.front())) {
        std::cerr << "Expected playback controller to start a track with a finishing backend.\n";
        return 1;
    }
    if (auto* track = library.findMutableTrack(ids.front())) {
        track->duration_seconds = 10;
    }
    playback.update(1.0, library);
    backend->finished = true;
    playback.update(0.016, library);
    if (!playback.session().current_track_id || *playback.session().current_track_id != ids.front()) {
        std::cerr << "Expected an early backend Finished state not to advance while known duration remains.\n";
        return 1;
    }
    if (!playback.session().finish_pending || playback.session().elapsed_seconds > 1.10) {
        std::cerr << "Expected early backend Finished state to remain on the current playback clock instead of projecting near EOF.\n";
        return 1;
    }
    playback.update(0.8, library);
    if (!playback.session().current_track_id || *playback.session().current_track_id != ids.front()) {
        std::cerr << "Expected early backend Finished state not to advance merely because a short elapsed interval passed.\n";
        return 1;
    }
    playback.update(6.7, library);
    if (!playback.session().current_track_id || *playback.session().current_track_id != ids.front()) {
        std::cerr << "Expected deferred finish not to advance on the same tick that first crosses the duration acceptance threshold.\n";
        return 1;
    }
    playback.update(0.1, library);
    if (!playback.session().current_track_id || *playback.session().current_track_id == ids.front()) {
        std::cerr << "Expected deferred finish to advance only once the playback clock reaches the duration acceptance threshold.\n";
        return 1;
    }

    if (!playback.startTrack(library, ids.front())) {
        std::cerr << "Expected playback controller to restart a track with a finishing backend.\n";
        return 1;
    }
    playback.update(4.0, library);
    backend->finished = true;
    playback.update(4.0, library);
    if (!playback.session().current_track_id || *playback.session().current_track_id != ids.front()) {
        std::cerr << "Expected backend Finished with more than the accepted remaining duration not to advance yet.\n";
        return 1;
    }
    playback.update(0.6, library);
    if (!playback.session().current_track_id || *playback.session().current_track_id != ids.front()) {
        std::cerr << "Expected backend Finished not to advance on the same tick that first crosses the duration acceptance threshold.\n";
        return 1;
    }
    playback.update(0.1, library);
    if (!playback.session().current_track_id || *playback.session().current_track_id == ids.front()) {
        std::cerr << "Expected normal playback finish to advance to next track when one exists.\n";
        return 1;
    }
    if (playback.session().elapsed_seconds > 0.01 || playback.session().status != lofibox::app::PlaybackStatus::Playing) {
        std::cerr << "Expected auto-advanced track to restart elapsed time and keep playing.\n";
        return 1;
    }

    if (!playback.startTrack(library, ids.back())) {
        std::cerr << "Expected last track to start.\n";
        return 1;
    }
    constexpr int last_track_duration_seconds = 10;
    if (auto* track = library.findMutableTrack(ids.back())) {
        track->duration_seconds = last_track_duration_seconds;
    }
    playback.setRepeatAll(false);
    backend->finished = true;
    playback.update(8.6, library);
    if (playback.session().status != lofibox::app::PlaybackStatus::Playing) {
        std::cerr << "Expected last-track early finish to remain active while known duration remains.\n";
        return 1;
    }
    playback.update(0.1, library);
    if (playback.session().status != lofibox::app::PlaybackStatus::Paused) {
        std::cerr << "Expected normal playback to pause when the last track finishes.\n";
        return 1;
    }
    if (playback.session().elapsed_seconds > static_cast<double>(last_track_duration_seconds) + 0.01) {
        std::cerr << "Expected elapsed time to remain bounded after final finish.\n";
        return 1;
    }

    playback.setRepeatAll(true);
    if (!playback.startTrack(library, ids.back())) {
        std::cerr << "Expected last track to restart for repeat-all test.\n";
        return 1;
    }
    (void)playback.stepQueue(library, 1);
    if (!playback.session().current_track_id || *playback.session().current_track_id != ids.front()) {
        std::cerr << "Expected repeat-all next from last track to wrap to first track.\n";
        return 1;
    }
    (void)playback.stepQueue(library, -1);
    if (!playback.session().current_track_id || *playback.session().current_track_id != ids.back()) {
        std::cerr << "Expected repeat-all previous from first track to wrap to last track.\n";
        return 1;
    }
    backend->finished = true;
    playback.update(8.6, library);
    if (!playback.session().current_track_id || *playback.session().current_track_id != ids.back()) {
        std::cerr << "Expected repeat-all last-track finish not to wrap before the duration acceptance threshold.\n";
        return 1;
    }
    playback.update(0.1, library);
    if (!playback.session().current_track_id || *playback.session().current_track_id != ids.front()) {
        std::cerr << "Expected repeat-all finish on last track to wrap to first track.\n";
        return 1;
    }

    playback.setRepeatOne(true);
    if (!playback.startTrack(library, ids[1])) {
        std::cerr << "Expected middle track to start for repeat-one test.\n";
        return 1;
    }
    if (auto* track = library.findMutableTrack(ids[1])) {
        track->duration_seconds = 10;
    }
    backend->finished = true;
    playback.update(8.6, library);
    if (!playback.session().current_track_id || *playback.session().current_track_id != ids[1]) {
        std::cerr << "Expected repeat-one finish not to restart before the duration acceptance threshold.\n";
        return 1;
    }
    playback.update(0.1, library);
    if (!playback.session().current_track_id || *playback.session().current_track_id != ids[1]) {
        std::cerr << "Expected repeat-one finish to restart the same track.\n";
        return 1;
    }
    if (playback.session().elapsed_seconds > 0.01 || playback.session().status != lofibox::app::PlaybackStatus::Playing) {
        std::cerr << "Expected repeat-one finish to restart playback from the beginning.\n";
        return 1;
    }

    playback.setRepeatOne(false);
    playback.setRepeatAll(true);
    if (!playback.startTrack(library, ids.front())) {
        std::cerr << "Expected first track to start for shuffle test.\n";
        return 1;
    }
    playback.setShuffleEnabled(true);
    if (!playback.session().shuffle_enabled) {
        std::cerr << "Expected shuffle mode to be enabled.\n";
        return 1;
    }
    std::set<int> visited{};
    visited.insert(*playback.session().current_track_id);
    for (int index = 0; index < static_cast<int>(ids.size()) * 2; ++index) {
        (void)playback.stepQueue(library, 1);
        if (playback.session().current_track_id) {
            visited.insert(*playback.session().current_track_id);
        }
    }
    if (visited.size() != ids.size()) {
        std::cerr << "Expected shuffle queue navigation to keep every track reachable.\n";
        return 1;
    }

    auto race_services = lofibox::app::withNullRuntimeServices();
    auto race_backend = std::make_shared<FinishOnSecondPollBackend>();
    race_services.playback.audio_backend = race_backend;
    lofibox::app::PlaybackController race_playback{};
    race_playback.setServices(race_services);
    library.setSongsContextAll();
    if (!race_playback.startTrack(library, ids.front())) {
        std::cerr << "Expected race playback controller to start a track.\n";
        return 1;
    }
    race_playback.update(0.016, library);
    if (!race_playback.session().current_track_id || *race_playback.session().current_track_id != ids.front()) {
        std::cerr << "Expected playback finish polling to be consumed at most once per UI update.\n";
        return 1;
    }
    race_playback.update(0.016, library);
    if (!race_playback.session().current_track_id || *race_playback.session().current_track_id != ids.front()) {
        std::cerr << "Expected early deferred finish polling not to skip the duration gate.\n";
        return 1;
    }
    race_playback.update(0.8, library);
    if (!race_playback.session().current_track_id || *race_playback.session().current_track_id != ids.front()) {
        std::cerr << "Expected deferred finish polling not to advance after a short elapsed interval.\n";
        return 1;
    }
    race_playback.update(7.8, library);
    if (!race_playback.session().current_track_id || *race_playback.session().current_track_id != ids.front()) {
        std::cerr << "Expected deferred finish polling not to advance on the same tick that first crosses the duration acceptance threshold.\n";
        return 1;
    }
    race_playback.update(0.1, library);
    if (!race_playback.session().current_track_id || *race_playback.session().current_track_id == ids.front()) {
        std::cerr << "Expected deferred finish polling to advance once the playback clock reaches the duration acceptance threshold.\n";
        return 1;
    }

    auto failed_services = lofibox::app::withNullRuntimeServices();
    auto failed_backend = std::make_shared<FailedAudioBackend>();
    failed_services.playback.audio_backend = failed_backend;
    lofibox::app::PlaybackController failed_playback{};
    failed_playback.setServices(failed_services);
    if (!failed_playback.startTrack(library, ids.front())) {
        std::cerr << "Expected failed backend test to start a track request.\n";
        return 1;
    }
    failed_playback.update(0.016, library);
    if (!failed_playback.session().current_track_id || *failed_playback.session().current_track_id != ids.front()) {
        std::cerr << "Expected playback backend failure not to auto-advance the queue.\n";
        return 1;
    }
    if (failed_playback.session().status != lofibox::app::PlaybackStatus::Paused) {
        std::cerr << "Expected playback backend failure to pause the current session.\n";
        return 1;
    }

    auto starting_services = lofibox::app::withNullRuntimeServices();
    auto starting_backend = std::make_shared<StartingThenPlayingBackend>();
    starting_services.playback.audio_backend = starting_backend;
    lofibox::app::PlaybackController starting_playback{};
    starting_playback.setServices(starting_services);
    if (!starting_playback.startTrack(library, ids.front())) {
        std::cerr << "Expected starting backend test to start a track request.\n";
        return 1;
    }
    starting_playback.update(12.0, library);
    if (starting_playback.session().elapsed_seconds > 0.01 || starting_playback.session().visualization_frame.available) {
        std::cerr << "Expected elapsed time and visualization to wait until backend audio is actually playing.\n";
        return 1;
    }
    starting_backend->state_value = lofibox::app::AudioPlaybackState::Playing;
    starting_playback.update(1.25, library);
    if (starting_playback.session().elapsed_seconds < 1.0 || !starting_playback.session().visualization_frame.available) {
        std::cerr << "Expected elapsed time and visualization to start after backend audio enters Playing.\n";
        return 1;
    }

    auto position_services = lofibox::app::withNullRuntimeServices();
    auto position_backend = std::make_shared<PositionReportingAudioBackend>();
    position_services.playback.audio_backend = position_backend;
    lofibox::app::PlaybackController position_playback{};
    position_playback.setServices(position_services);
    if (!position_playback.startTrack(library, ids.front())) {
        std::cerr << "Expected position-reporting backend test to start a track request.\n";
        return 1;
    }
    if (auto* track = library.findMutableTrack(ids.front())) {
        track->duration_seconds = 20;
    }
    position_backend->position_seconds = 12.25;
    position_playback.update(0.1, library);
    if (position_playback.session().elapsed_seconds < 12.24 || position_playback.session().elapsed_seconds > 12.26) {
        std::cerr << "Expected playback session clock to use backend position when available.\n";
        return 1;
    }
    position_backend->position_seconds = 13.0;
    position_playback.update(10.0, library);
    if (position_playback.session().elapsed_seconds < 12.99 || position_playback.session().elapsed_seconds > 13.01) {
        std::cerr << "Expected backend position to remain the playback clock truth instead of main-loop delta.\n";
        return 1;
    }
    position_backend->state_value = lofibox::app::AudioPlaybackState::Finished;
    position_backend->position_seconds = 7.0;
    position_playback.update(10.0, library);
    if (!position_playback.session().current_track_id || *position_playback.session().current_track_id != ids.front()) {
        std::cerr << "Expected early Finished with a backend position far from duration not to auto-advance.\n";
        return 1;
    }
    position_playback.update(0.5, library);
    if (!position_playback.session().current_track_id || *position_playback.session().current_track_id != ids.front()) {
        std::cerr << "Expected early Finished with backend position far from duration to wait for a short confirmation window.\n";
        return 1;
    }
    for (int tick = 0; tick < 4; ++tick) {
        position_playback.update(0.25, library);
    }
    if (!position_playback.session().current_track_id
        || *position_playback.session().current_track_id != ids.front()
        || position_playback.session().status != lofibox::app::PlaybackStatus::Paused
        || position_playback.session().audio_active) {
        std::cerr << "Expected early backend-position finish to pause the current track instead of auto-advancing.\n";
        return 1;
    }

    if (!position_playback.startTrack(library, ids.front())) {
        std::cerr << "Expected position-reporting backend test to restart after early backend-position rejection.\n";
        return 1;
    }
    if (auto* track = library.findMutableTrack(ids.front())) {
        track->duration_seconds = 20;
    }
    position_backend->position_seconds = 13.0;
    position_playback.update(0.1, library);
    position_backend->state_value = lofibox::app::AudioPlaybackState::Finished;
    position_backend->position_seconds = 18.49;
    position_playback.update(0.1, library);
    if (!position_playback.session().current_track_id || *position_playback.session().current_track_id != ids.front()) {
        std::cerr << "Expected Finished with more than the accepted remaining backend position not to auto-advance.\n";
        return 1;
    }
    position_backend->position_seconds = 18.51;
    position_playback.update(0.1, library);
    if (!position_playback.session().current_track_id || *position_playback.session().current_track_id == ids.front()) {
        std::cerr << "Expected Finished with backend position inside the acceptance threshold to auto-advance.\n";
        return 1;
    }

    const fs::path remote_cache_root = fs::temp_directory_path() / "lofibox_zero_remote_governance_smoke";
    fs::remove_all(remote_cache_root, ec);
    auto remote_services = lofibox::app::withNullRuntimeServices();
    auto remote_backend = std::make_shared<RemoteAudioBackend>();
    remote_services.playback.audio_backend = remote_backend;
    remote_services.metadata.track_identity_provider = std::make_shared<RemoteIdentityProvider>();
    remote_services.metadata.lyrics_provider = std::make_shared<RemoteLyricsProvider>();
    remote_services.cache.cache_manager = std::make_shared<lofibox::cache::CacheManager>(remote_cache_root);
    lofibox::app::LibraryController remote_library{};
    const lofibox::app::RemoteServerProfile remote_profile{
        lofibox::app::RemoteServerKind::DirectUrl,
        "direct",
        "Remote Source",
        "https://example.test/audio.mp3"};
    const lofibox::app::RemoteTrack remote_track{
        "remote-track-1",
        "UNKNOWN",
        "UNKNOWN",
        "",
        "",
        0,
        "direct",
        "Remote Source"};
    remote_library.mergeRemoteTracks(remote_profile, {remote_track});
    const auto remote_ids = remote_library.allSongIdsSorted();
    if (remote_ids.empty()) {
        std::cerr << "Expected remote governance test track to enter Library.\n";
        return 1;
    }
    auto* remote_record = remote_library.findMutableTrack(remote_ids.front());
    if (remote_record == nullptr) {
        std::cerr << "Expected remote governance test track to be mutable.\n";
        return 1;
    }
    lofibox::app::PlaybackController remote_playback{};
    remote_playback.setServices(remote_services);
    lofibox::app::ResolvedRemoteStream remote_stream{};
    remote_stream.url = "https://example.test/audio.mp3";
    remote_stream.diagnostics.connected = true;
    remote_stream.diagnostics.connection_status = "READY";
    if (!remote_playback.startRemoteLibraryTrack(remote_stream, *remote_record, remote_profile, remote_track, "Remote Source", true)) {
        std::cerr << "Expected remote Library playback to start.\n";
        return 1;
    }
    for (int attempt = 0; attempt < 50; ++attempt) {
        remote_playback.update(0.02, remote_library);
        const auto* governed = remote_library.findTrack(remote_ids.front());
        if (governed != nullptr && governed->title == "Governed Song") {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds{10});
    }
    const auto* governed = remote_library.findTrack(remote_ids.front());
    if (governed == nullptr
        || governed->title != "Governed Song"
        || governed->artist != "Governed Artist"
        || governed->album != "Governed Album"
        || governed->lyrics_plain != "governed remote lyric"
        || governed->fingerprint != "remote-fingerprint-123") {
        std::cerr << "Expected read-only remote playback to govern metadata and lyrics locally.\n";
        return 1;
    }
    const auto cached_remote = remote_services.cache.cache_manager->getText(
        lofibox::cache::CacheBucket::Metadata,
        "remote-media-direct-url-direct-remote-track-1");
    if (!cached_remote
        || cached_remote->find("Governed Song") == std::string::npos
        || cached_remote->find("remote-fingerprint-123") == std::string::npos) {
        std::cerr << "Expected governed remote metadata to persist in the local cache bucket.\n";
        return 1;
    }

    auto fingerprint_only_services = lofibox::app::withNullRuntimeServices();
    fingerprint_only_services.playback.audio_backend = std::make_shared<RemoteAudioBackend>();
    fingerprint_only_services.metadata.track_identity_provider = std::make_shared<FingerprintOnlyRemoteIdentityProvider>();
    fingerprint_only_services.cache.cache_manager = std::make_shared<lofibox::cache::CacheManager>(remote_cache_root);
    lofibox::app::LibraryController fingerprint_library{};
    const lofibox::app::RemoteTrack fingerprint_track{
        "remote-track-2",
        "Already Named",
        "Already Known Artist",
        "Known Album",
        "",
        200,
        "direct",
        "Remote Source"};
    fingerprint_library.mergeRemoteTracks(remote_profile, {fingerprint_track});
    const auto fingerprint_ids = fingerprint_library.allSongIdsSorted();
    auto* fingerprint_record = fingerprint_ids.empty() ? nullptr : fingerprint_library.findMutableTrack(fingerprint_ids.front());
    lofibox::app::PlaybackController fingerprint_playback{};
    fingerprint_playback.setServices(fingerprint_only_services);
    if (fingerprint_record == nullptr
        || !fingerprint_playback.startRemoteLibraryTrack(remote_stream, *fingerprint_record, remote_profile, fingerprint_track, "Remote Source", true)) {
        std::cerr << "Expected fingerprint-only remote playback to start.\n";
        return 1;
    }
    for (int attempt = 0; attempt < 50; ++attempt) {
        fingerprint_playback.update(0.02, fingerprint_library);
        const auto* updated = fingerprint_library.findTrack(fingerprint_ids.front());
        if (updated != nullptr && updated->fingerprint == "fingerprint-without-online-identity") {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds{10});
    }
    const auto* fingerprint_governed = fingerprint_library.findTrack(fingerprint_ids.front());
    if (fingerprint_governed == nullptr || fingerprint_governed->fingerprint != "fingerprint-without-online-identity") {
        std::cerr << "Expected remote audio fingerprint to persist even when online identity lookup misses.\n";
        return 1;
    }
    fs::remove_all(remote_cache_root, ec);

    fs::remove_all(root, ec);
    return 0;
}

