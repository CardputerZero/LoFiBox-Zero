// SPDX-License-Identifier: GPL-3.0-or-later

#include "app/lofibox_app_runner.h"

#include <chrono>
#include <thread>

#include "app/lofibox_app.h"
#include "core/display_profile.h"

namespace lofibox::app {

void runLoFiBoxApp(
    platform::SurfacePresenter& presenter,
    ui::UiAssets assets,
    RuntimeServices services,
    std::chrono::milliseconds auto_exit_after,
    std::vector<std::string> startup_uris,
    AppStarter on_start
)
{
    LoFiBoxApp app{{}, std::move(assets), std::move(services), std::move(startup_uris), true, std::move(on_start)};
    core::Canvas canvas{core::kDisplayWidth, core::kDisplayHeight};

    using clock = std::chrono::steady_clock;
    constexpr auto kFrameTime = std::chrono::milliseconds(33);
    auto next_frame = clock::now();
    const auto started_at = next_frame;

    while (presenter.pump()) {
        for (const auto& event : presenter.drainInput()) {
            app.handleInput(event);
        }

        if (auto_exit_after > std::chrono::milliseconds::zero() && (clock::now() - started_at) >= auto_exit_after) {
            break;
        }
        if (app.runtimeShutdownRequested()) {
            break;
        }

        app.update();
        app.render(canvas);
        presenter.present(canvas);

        next_frame += kFrameTime;
        std::this_thread::sleep_until(next_frame);

        if (clock::now() > next_frame + std::chrono::milliseconds(250)) {
            next_frame = clock::now();
        }
    }
}

} // namespace lofibox::app
