// SPDX-License-Identifier: GPL-3.0-or-later

#include "webui/webui_runtime_adapter.h"

#include "runtime/runtime_command_client.h"

namespace lofibox::webui {

WebUiRuntimeAdapter::WebUiRuntimeAdapter(runtime::RuntimeCommandClient& client)
    : client_(client)
{
}

runtime::RuntimeSnapshot WebUiRuntimeAdapter::querySnapshot()
{
    try {
        current_snapshot_ = client_.snapshot();
        last_error_.clear();
        connected_ = true;
    } catch (const std::exception& ex) {
        last_error_ = ex.what();
        connected_ = false;
    }
    return current_snapshot_;
}

runtime::RuntimeCommandResult WebUiRuntimeAdapter::submitCommand(runtime::RuntimeCommand command)
{
    try {
        auto result = client_.dispatch(command);
        if (!result.accepted) {
            last_error_ = result.message;
        } else {
            last_error_.clear();
        }
        return result;
    } catch (const std::exception& ex) {
        last_error_ = ex.what();
        connected_ = false;
        return runtime::RuntimeCommandResult::reject("exception", ex.what(), command.origin, command.correlation_id, 0, 0);
    }
}

std::vector<runtime::RuntimeEvent> WebUiRuntimeAdapter::pollEvents()
{
    std::vector<runtime::RuntimeEvent> events;
    try {
        current_snapshot_ = client_.snapshot();
        last_error_.clear();
        connected_ = true;

        const auto timestamp = runtime::runtimeEventTimestampMs();
        events = runtime::runtimeEventsBetween(previous_snapshot_, current_snapshot_, timestamp);
        previous_snapshot_ = current_snapshot_;
    } catch (const std::exception& ex) {
        last_error_ = ex.what();
        connected_ = false;
    }
    return events;
}

const runtime::RuntimeSnapshot& WebUiRuntimeAdapter::lastSnapshot() const noexcept
{
    return current_snapshot_;
}

const std::string& WebUiRuntimeAdapter::lastError() const noexcept
{
    return last_error_;
}

bool WebUiRuntimeAdapter::isConnected() const noexcept
{
    return connected_;
}

} // namespace lofibox::webui
