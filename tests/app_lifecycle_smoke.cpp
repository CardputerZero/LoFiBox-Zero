// SPDX-License-Identifier: GPL-3.0-or-later

#include <chrono>
#include <iostream>

#include "app/app_lifecycle.h"

namespace {

class FakeLifecycleTarget final : public lofibox::app::AppLifecycleTarget {
public:
    [[nodiscard]] std::chrono::steady_clock::time_point lastUpdate() const noexcept override { return last_update; }
    void setLastUpdate(std::chrono::steady_clock::time_point value) noexcept override { last_update = value; }
    void refreshRuntimeStatusIfDue() override { ++refresh_runtime_calls; }

    [[nodiscard]] lofibox::app::LibraryIndexState libraryState() const noexcept override { return library_state; }
    void startLibraryLoading() override
    {
        ++start_loading_calls;
        library_state = lofibox::app::LibraryIndexState::Loading;
        async_ticks_remaining = kAsyncScanTicks;
    }
    void refreshLibrary() override
    {
        ++refresh_library_calls;
        if (async_ticks_remaining > 0) {
            --async_ticks_remaining;
        }
        if (async_ticks_remaining == 0) {
            library_state = lofibox::app::LibraryIndexState::Ready;
        }
    }

    [[nodiscard]] lofibox::app::AppPage currentPage() const noexcept override { return page; }
    [[nodiscard]] bool bootAnimationComplete() const override { return boot_animation_complete; }
    void showMainMenu() override { ++show_main_menu_calls; page = lofibox::app::AppPage::MainMenu; }

    std::chrono::steady_clock::time_point last_update{std::chrono::steady_clock::now() - std::chrono::milliseconds{16}};
    lofibox::app::LibraryIndexState library_state{lofibox::app::LibraryIndexState::Ready};
    lofibox::app::AppPage page{lofibox::app::AppPage::MainMenu};
    bool boot_animation_complete{true};
    int refresh_runtime_calls{0};
    int start_loading_calls{0};
    int refresh_library_calls{0};
    int show_main_menu_calls{0};
    int async_ticks_remaining{0};

    static constexpr int kAsyncScanTicks = 4;
};

} // namespace

int main()
{
    // 1. Uninitialized -> should trigger startLibraryLoading
    {
        FakeLifecycleTarget target{};
        target.library_state = lofibox::app::LibraryIndexState::Uninitialized;
        lofibox::app::updateAppLifecycle(target);
        if (target.refresh_runtime_calls != 1 || target.start_loading_calls != 1) {
            std::cerr << "Expected uninitialized library to enter loading without owning runtime playback tick.\n";
            return 1;
        }
        if (target.library_state != lofibox::app::LibraryIndexState::Loading) {
            std::cerr << "Expected startLibraryLoading to transition to Loading.\n";
            return 1;
        }
    }

    // 2. Loading -> refreshLibrary called each tick, stays Loading until scan completes
    {
        FakeLifecycleTarget target{};
        target.library_state = lofibox::app::LibraryIndexState::Loading;
        target.async_ticks_remaining = target.kAsyncScanTicks;

        for (int tick = 0; tick < target.kAsyncScanTicks; ++tick) {
            lofibox::app::updateAppLifecycle(target);
            if (tick < target.kAsyncScanTicks - 1 && target.library_state != lofibox::app::LibraryIndexState::Loading) {
                std::cerr << "Expected library state to remain Loading until async scan completes.\n";
                return 1;
            }
        }
        if (target.library_state != lofibox::app::LibraryIndexState::Ready) {
            std::cerr << "Expected library state to become Ready after async scan completes.\n";
            return 1;
        }
        if (target.refresh_library_calls != target.kAsyncScanTicks) {
            std::cerr << "Expected refreshLibrary to be called each tick while Loading.\n";
            return 1;
        }
    }

    // 3. Boot page -> transitions to MainMenu when library ready and animation complete
    {
        FakeLifecycleTarget target{};
        target.page = lofibox::app::AppPage::Boot;
        target.library_state = lofibox::app::LibraryIndexState::Ready;
        target.boot_animation_complete = true;
        lofibox::app::updateAppLifecycle(target);
        if (target.show_main_menu_calls != 1 || target.page != lofibox::app::AppPage::MainMenu) {
            std::cerr << "Expected completed boot page to advance to main menu.\n";
            return 1;
        }
    }

    // 4. Boot page with animation not complete -> stays on boot
    {
        FakeLifecycleTarget target{};
        target.page = lofibox::app::AppPage::Boot;
        target.library_state = lofibox::app::LibraryIndexState::Ready;
        target.boot_animation_complete = false;
        lofibox::app::updateAppLifecycle(target);
        if (target.show_main_menu_calls != 0 || target.page != lofibox::app::AppPage::Boot) {
            std::cerr << "Expected boot page to persist while animation is incomplete.\n";
            return 1;
        }
    }

    return 0;
}
