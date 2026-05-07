// SPDX-License-Identifier: GPL-3.0-or-later

#include "platform/host/single_instance_lock.h"

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <memory>
#include <string>
#include <thread>

namespace {

std::string uniqueSuffix()
{
    const auto ticks = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto thread_id = std::hash<std::thread::id>{}(std::this_thread::get_id());
    return std::to_string(ticks) + "-" + std::to_string(thread_id);
}

void configureLinuxLockPath(const std::string& suffix)
{
#if defined(__linux__)
    const std::string path = "/tmp/lofibox-single-instance-lock-smoke-" + suffix + ".lock";
    setenv("LOFIBOX_SINGLE_INSTANCE_LOCK", path.c_str(), 1);
#else
    (void)suffix;
#endif
}

void startWatchdog(std::shared_ptr<std::atomic_bool> completed)
{
    std::thread([completed = std::move(completed)] {
        std::this_thread::sleep_for(std::chrono::seconds{5});
        if (!completed->load()) {
            std::cerr << "single-instance lock smoke timed out; acquire must be non-blocking\n";
            std::_Exit(124);
        }
    }).detach();
}

} // namespace

int main()
{
    auto completed = std::make_shared<std::atomic_bool>(false);
    startWatchdog(completed);

    const auto suffix = uniqueSuffix();
    const std::string instance_name = "lofibox-zero-test-lock-" + suffix;
    configureLinuxLockPath(suffix);

    {
        auto first = lofibox::platform::host::SingleInstanceLock::acquire(instance_name);
        if (!first.acquired()) {
            std::cerr << "first lock acquisition failed: " << first.message() << '\n';
            completed->store(true);
            return 1;
        }

        auto second = lofibox::platform::host::SingleInstanceLock::acquire(instance_name);
        if (second.acquired()) {
            std::cerr << "second lock acquisition should have been rejected\n";
            completed->store(true);
            return 1;
        }
    }

    completed->store(true);
    return 0;
}
