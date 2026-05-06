// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <chrono>
#include <string>
#include <vector>

#include "app/lofibox_app.h"
#include "platform/surface_presenter.h"

namespace lofibox::app {

void runLoFiBoxApp(
    platform::SurfacePresenter& presenter,
    ui::UiAssets assets = {},
    RuntimeServices services = {},
    std::chrono::milliseconds auto_exit_after = std::chrono::milliseconds::zero(),
    std::vector<std::string> startup_uris = {},
    AppStarter on_start = {}
);

} // namespace lofibox::app
