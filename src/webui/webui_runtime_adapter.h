// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <chrono>
#include <memory>
#include <mutex>
#include <optional>
#include <string>

#include "runtime/runtime_command.h"
#include "runtime/runtime_event.h"
#include "runtime/runtime_result.h"
#include "runtime/runtime_snapshot.h"

namespace lofibox::runtime {
class RuntimeCommandClient;
} // namespace lofibox::runtime

namespace lofibox::webui {

// Sole point of contact with the runtime layer.
// WebUI must not call RuntimeCommandClient or RuntimeSnapshot directly
// outside of this adapter.
class WebUiRuntimeAdapter {
public:
    explicit WebUiRuntimeAdapter(runtime::RuntimeCommandClient& client);

    // Query the current full runtime snapshot.
    runtime::RuntimeSnapshot querySnapshot();

    // Submit a runtime command and return the result.
    runtime::RuntimeCommandResult submitCommand(runtime::RuntimeCommand command);

    // Poll for events by comparing the previous snapshot with the current one.
    // Returns a vector of events that occurred since the last poll.
    // Updates the internal snapshot cache.
    std::vector<runtime::RuntimeEvent> pollEvents();

    // Get the last cached snapshot without a new query.
    const runtime::RuntimeSnapshot& lastSnapshot() const noexcept;

    // Get the last error message, if any.
    const std::string& lastError() const noexcept;

    // Check if the runtime connection is healthy.
    bool isConnected() const noexcept;

private:
    runtime::RuntimeCommandClient& client_;
    std::optional<runtime::RuntimeSnapshot> previous_snapshot_{};
    runtime::RuntimeSnapshot current_snapshot_{};
    std::string last_error_{};
    bool connected_{false};
};

} // namespace lofibox::webui
