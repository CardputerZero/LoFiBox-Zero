// SPDX-License-Identifier: GPL-3.0-or-later

#include <iostream>
#include <string>
#include <string_view>

#include "app/app_input_router.h"

namespace {

class FakeTarget final : public lofibox::app::AppInputTarget {
public:
    [[nodiscard]] lofibox::app::AppPage currentPage() const noexcept override { return page; }
    [[nodiscard]] bool helpOpen() const noexcept override { return help_open; }
    [[nodiscard]] bool isBrowseListPage() const noexcept override { return browse_list_page; }
    [[nodiscard]] bool nowPlayingConfirmBlocked() const override { return now_playing_confirm_blocked; }

    void toggleHelpForCurrentPage() override { ++toggle_help_calls; help_open = !help_open; }
    void closeHelp() override { ++close_help_calls; help_open = false; }
    void pushPage(lofibox::app::AppPage next_page) override { ++push_page_calls; page = next_page; }
    void popPage() override { ++pop_page_calls; page = lofibox::app::AppPage::MainMenu; }

    void playFromMenu() override { ++play_from_menu_calls; }
    void pausePlayback() override { ++pause_calls; }
    void stepTrack(int delta) override { last_step_delta = delta; ++step_track_calls; }
    void cycleMainMenuPlaybackMode() override { ++cycle_menu_mode_calls; }
    void toggleRepeatAll() override { ++toggle_repeat_all_calls; }
    void toggleRepeatOne() override { ++toggle_repeat_one_calls; }
    void openSearchPage() override { ++open_search_calls; page = lofibox::app::AppPage::Search; }
    void openLibraryPage() override { ++open_library_calls; page = lofibox::app::AppPage::MusicIndex; }
    void openQueuePage() override { ++open_queue_calls; page = lofibox::app::AppPage::Queue; }
    void openSettingsPage() override { ++open_settings_calls; page = lofibox::app::AppPage::Settings; }
    void showMainMenuPage() override { ++show_main_menu_calls; page = lofibox::app::AppPage::MainMenu; }
    void moveMainMenuSelection(int delta) override { main_menu_delta += delta; }
    void resetMainMenuSelection() override { ++reset_main_menu_calls; }
    void confirmMainMenu() override { ++confirm_main_menu_calls; }

    void toggleShuffle() override { ++toggle_shuffle_calls; }
    void cycleRepeatMode() override { ++cycle_repeat_calls; }
    void togglePlayPause() override { ++toggle_play_pause_calls; }
    void cycleAudioEffect() override { ++cycle_audio_effect_calls; }

    void moveEqualizerSelection(int delta) override { eq_selection_delta += delta; }
    void adjustSelectedEqualizerBand(int delta) override { eq_band_delta += delta; }
    void cycleEqualizerPreset(int delta) override { eq_preset_delta += delta; }

    void cycleSongSortModeAndClamp() override { ++cycle_sort_calls; }
    void moveSelection(int delta) override { list_delta += delta; }
    void moveSelectionPage(int delta_pages) override { page_delta += delta_pages; }
    void confirmListPage() override { ++confirm_list_calls; }
    void appendSearchText(std::string_view text) override { search_text += text; }
    void appendRemoteProfileEditText(std::string_view text) override { remote_edit_text += text; }

    lofibox::app::AppPage page{lofibox::app::AppPage::MainMenu};
    bool help_open{false};
    bool browse_list_page{false};
    bool now_playing_confirm_blocked{false};
    int toggle_help_calls{0};
    int close_help_calls{0};
    int push_page_calls{0};
    int pop_page_calls{0};
    int play_from_menu_calls{0};
    int pause_calls{0};
    int step_track_calls{0};
    int last_step_delta{0};
    int cycle_menu_mode_calls{0};
    int toggle_repeat_all_calls{0};
    int toggle_repeat_one_calls{0};
    int open_search_calls{0};
    int open_library_calls{0};
    int open_queue_calls{0};
    int open_settings_calls{0};
    int show_main_menu_calls{0};
    int main_menu_delta{0};
    int reset_main_menu_calls{0};
    int confirm_main_menu_calls{0};
    int toggle_shuffle_calls{0};
    int cycle_repeat_calls{0};
    int toggle_play_pause_calls{0};
    int cycle_audio_effect_calls{0};
    int eq_selection_delta{0};
    int eq_band_delta{0};
    int eq_preset_delta{0};
    int cycle_sort_calls{0};
    int list_delta{0};
    int page_delta{0};
    int confirm_list_calls{0};
    std::string search_text{};
    std::string remote_edit_text{};
};

[[nodiscard]] lofibox::app::InputEvent key(lofibox::app::InputKey input_key)
{
    return lofibox::app::InputEvent{input_key, "", '\0'};
}

[[nodiscard]] lofibox::app::InputEvent character(char value)
{
    return lofibox::app::InputEvent{lofibox::app::InputKey::Character, "", value};
}

} // namespace

int main()
{
    FakeTarget target{};

    lofibox::app::routeInput(target, key(lofibox::app::InputKey::F1));
    if (!target.help_open || target.toggle_help_calls != 1) {
        std::cerr << "Expected F1 to toggle page help.\n";
        return 1;
    }

    lofibox::app::routeInput(target, key(lofibox::app::InputKey::Enter));
    if (target.help_open || target.close_help_calls != 1) {
        std::cerr << "Expected Enter to close open help.\n";
        return 1;
    }

    lofibox::app::routeInput(target, key(lofibox::app::InputKey::F2));
    lofibox::app::routeInput(target, key(lofibox::app::InputKey::F3));
    lofibox::app::routeInput(target, key(lofibox::app::InputKey::F4));
    lofibox::app::routeInput(target, key(lofibox::app::InputKey::F5));
    lofibox::app::routeInput(target, key(lofibox::app::InputKey::F6));
    lofibox::app::routeInput(target, key(lofibox::app::InputKey::F7));
    lofibox::app::routeInput(target, key(lofibox::app::InputKey::F8));
    lofibox::app::routeInput(target, character('R'));
    lofibox::app::routeInput(target, key(lofibox::app::InputKey::Right));
    if (target.play_from_menu_calls != 1 || target.pause_calls != 1 || target.last_step_delta != 1 || target.step_track_calls != 2 || target.cycle_menu_mode_calls != 1 || target.toggle_shuffle_calls != 0 || target.toggle_repeat_all_calls != 0 || target.toggle_repeat_one_calls != 0 || target.cycle_audio_effect_calls != 1 || target.main_menu_delta != 1) {
        std::cerr << "Expected main menu shortcuts and navigation to route to menu commands.\n";
        return 1;
    }

    target.page = lofibox::app::AppPage::NowPlaying;
    lofibox::app::routeInput(target, character('L'));
    if (target.page != lofibox::app::AppPage::Lyrics || target.push_page_calls != 1) {
        std::cerr << "Expected Now Playing L to push lyrics page.\n";
        return 1;
    }

    lofibox::app::routeInput(target, key(lofibox::app::InputKey::Left));
    if (target.last_step_delta != -1) {
        std::cerr << "Expected lyrics page transport controls to route.\n";
        return 1;
    }

    target.page = lofibox::app::AppPage::NowPlaying;
    lofibox::app::routeInput(target, key(lofibox::app::InputKey::Down));
    lofibox::app::routeInput(target, key(lofibox::app::InputKey::Up));
    if (target.cycle_menu_mode_calls != 3 || target.cycle_repeat_calls != 0 || target.toggle_shuffle_calls != 0) {
        std::cerr << "Expected Now Playing Up/Down to cycle the unified playback mode.\n";
        return 1;
    }

    target.page = lofibox::app::AppPage::Equalizer;
    lofibox::app::routeInput(target, key(lofibox::app::InputKey::Right));
    lofibox::app::routeInput(target, key(lofibox::app::InputKey::Up));
    lofibox::app::routeInput(target, key(lofibox::app::InputKey::Enter));
    if (target.eq_selection_delta != 1 || target.eq_band_delta != 1 || target.eq_preset_delta != 1) {
        std::cerr << "Expected equalizer page controls to route.\n";
        return 1;
    }

    target.page = lofibox::app::AppPage::Search;
    const std::string committed_cjk = "\xE4\xB8\xAD\xE6\x96\x87";
    lofibox::app::routeInput(target, lofibox::app::makeCommittedTextInput(committed_cjk, "IME"));
    if (target.search_text != committed_cjk) {
        std::cerr << "Expected Search page to receive committed UTF-8 text.\n";
        return 1;
    }

    target.page = lofibox::app::AppPage::Songs;
    target.browse_list_page = true;
    const int pause_calls_before = target.pause_calls;
    const int sort_calls_before = target.cycle_sort_calls;
    lofibox::app::routeInput(target, key(lofibox::app::InputKey::F3));
    lofibox::app::routeInput(target, key(lofibox::app::InputKey::PageDown));
    lofibox::app::routeInput(target, character('T'));
    const int list_delta_before_x = target.list_delta;
    lofibox::app::routeInput(target, character('X'));
    lofibox::app::routeInput(target, key(lofibox::app::InputKey::Down));
    lofibox::app::routeInput(target, key(lofibox::app::InputKey::Enter));
    if (target.pause_calls != pause_calls_before + 1 || target.cycle_sort_calls != sort_calls_before + 1 || target.page_delta != 1 || target.list_delta != list_delta_before_x + 1 || target.confirm_list_calls != 1) {
        std::cerr << "Expected browse list transport shortcuts to stay global while list and page navigation still route.\n";
        return 1;
    }

    lofibox::app::routeInput(target, key(lofibox::app::InputKey::F9));
    lofibox::app::routeInput(target, key(lofibox::app::InputKey::F10));
    lofibox::app::routeInput(target, key(lofibox::app::InputKey::F11));
    lofibox::app::routeInput(target, key(lofibox::app::InputKey::F12));
    if (target.open_search_calls != 1 || target.open_library_calls != 1 || target.open_queue_calls != 1 || target.open_settings_calls != 1) {
        std::cerr << "Expected global F9-F12 page shortcuts to route.\n";
        return 1;
    }

    return 0;
}
