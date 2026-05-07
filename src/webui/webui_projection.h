// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <string>

#include "runtime/runtime_event.h"
#include "runtime/runtime_snapshot.h"

namespace lofibox::webui {

// === WebUI JSON DTO builders ===
// Each produces a self-contained JSON string from the corresponding
// RuntimeSnapshot section. These are structured projections, not
// pre-rendered GUI text.

std::string buildNowPlayingJson(const runtime::RuntimeSnapshot& snapshot);
std::string buildQueueJson(const runtime::RuntimeSnapshot& snapshot);
std::string buildLibraryJson(const runtime::RuntimeSnapshot& snapshot);
std::string buildSourcesJson(const runtime::RuntimeSnapshot& snapshot);
std::string buildEqJson(const runtime::RuntimeSnapshot& snapshot);
std::string buildSettingsJson(const runtime::RuntimeSnapshot& snapshot);
std::string buildDiagnosticsJson(const runtime::RuntimeSnapshot& snapshot);

// Full snapshot projection (all sections).
std::string buildFullSnapshotJson(const runtime::RuntimeSnapshot& snapshot);

// Event projection (for WebSocket push).
std::string buildEventJson(const runtime::RuntimeEvent& event);

} // namespace lofibox::webui
