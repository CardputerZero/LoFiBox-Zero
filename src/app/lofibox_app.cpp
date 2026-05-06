// SPDX-License-Identifier: GPL-3.0-or-later

#include "app/lofibox_app.h"

#include <utility>

#include "app/app_runtime_context.h"

namespace lofibox::app {

LoFiBoxApp::LoFiBoxApp(
    std::vector<std::filesystem::path> media_roots,
    ui::UiAssets assets,
    RuntimeServices services,
    std::vector<std::string> startup_uris,
    bool enable_external_runtime_transport,
    AppStarter on_start
    )
    : services_(withNullRuntimeServices(std::move(services))),
      app_host_(services_),
      runtime_host_(app_host_.registry()),
      context_(std::make_unique<AppRuntimeContext>(
          std::move(media_roots),
          std::move(assets),
          app_host_,
          runtime_host_.client(),
          std::move(startup_uris)))
{
    runtime_host_.resetEq();
    if (enable_external_runtime_transport) {
        (void)runtime_host_.startExternalTransport();
    }

    if (on_start) {
        on_start(runtime_host_.client(), app_host_.registry());
        subsystems_ = std::move(on_start);
    }
}

LoFiBoxApp::~LoFiBoxApp() = default;

void LoFiBoxApp::update()
{
    const auto now = clock::now();
    const double delta_seconds = std::chrono::duration<double>(now - last_runtime_tick_).count();
    last_runtime_tick_ = now;
    runtime_host_.tick(delta_seconds);
    context_->update();
}

void LoFiBoxApp::handleInput(const InputEvent& event)
{
    context_->handleInput(event);
}

void LoFiBoxApp::render(core::Canvas& canvas) const
{
    context_->render(canvas);
}

AppDebugSnapshot LoFiBoxApp::snapshot() const
{
    return context_->snapshot();
}

bool LoFiBoxApp::runtimeShutdownRequested() const noexcept
{
    return runtime_host_.shutdownRequested();
}

} // namespace lofibox::app
