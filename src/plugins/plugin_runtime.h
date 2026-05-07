// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <atomic>
#include <chrono>
#include <filesystem>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <vector>

namespace lofibox::plugins {

struct PluginRuntimeConfig {
    std::filesystem::path plugin_dir{};
    std::filesystem::path stderr_log_dir{};
    std::string command{"python3"};
    std::vector<std::string> args{};
    std::optional<std::string> cwd{};
    std::unordered_map<std::string, std::string> env{};
    std::chrono::milliseconds request_timeout{30000};
    std::chrono::milliseconds ready_timeout{10000};
    int max_restarts_per_minute{3};
};

// Pipe layout for the external helper process:
//
//   Parent (LoFiBox)              Child (helper process)
//   stdin_fd_  -> pipe A -> stdin  (JSON-RPC)
//   stdout_fd_ <- pipe B <- stdout (JSON-RPC)
//   stderr_fd_ <- pipe C <- stderr (log only)
//
// stdout and stderr are separate pipes. No log output can corrupt
// JSON-RPC response parsing. Stderr is read asynchronously on a
// dedicated thread and written to stderr.log under the configured log dir.
class PluginRuntime {
public:
    explicit PluginRuntime(PluginRuntimeConfig config);
    ~PluginRuntime();

    PluginRuntime(const PluginRuntime&) = delete;
    PluginRuntime& operator=(const PluginRuntime&) = delete;
    PluginRuntime(PluginRuntime&&) noexcept;
    PluginRuntime& operator=(PluginRuntime&&) noexcept;

    bool start();
    void stop();
    [[nodiscard]] bool isRunning() const;

    // Send a JSON-RPC request, returns the JSON response string or nullopt on failure.
    [[nodiscard]] std::optional<std::string> call(
        std::string_view method,
        std::string_view params_json);

    [[nodiscard]] const std::string& lastError() const noexcept { return last_error_; }
    [[nodiscard]] const PluginRuntimeConfig& config() const noexcept { return config_; }

    [[nodiscard]] bool isHealthy() const noexcept { return healthy_.load(std::memory_order_acquire); }

private:
    bool spawnProcess();
    bool waitForReady();
    void killProcess();
    bool tryRestart();
    std::string readResponse(int request_id);
    std::optional<std::string> readLine(std::chrono::milliseconds timeout);
    bool writeAll(std::string_view data);
    void startStderrReader();
    void stopStderrReader();

    PluginRuntimeConfig config_{};
    int pid_{-1};
    int stdin_fd_{-1};
    int stdout_fd_{-1};
    int stderr_fd_{-1};
    int next_request_id_{1};
    std::string stdout_buffer_{};
    std::string last_error_{};
    int restart_count_{0};
    std::chrono::steady_clock::time_point restart_window_start_{};
    std::thread stderr_thread_{};
    std::atomic<bool> stderr_running_{false};
    std::atomic<bool> healthy_{false};
    mutable std::mutex call_mutex_{};
};

} // namespace lofibox::plugins
