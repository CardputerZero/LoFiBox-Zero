// SPDX-License-Identifier: GPL-3.0-or-later

#include "plugins/plugin_runtime.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <thread>

#if defined(__linux__)
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace lofibox::plugins {
namespace {

constexpr int kMaxResponseBytes = 262144;

std::string buildJsonRpcRequest(std::string_view method, std::string_view params_json, int id)
{
    std::ostringstream out;
    out << "{\"jsonrpc\":\"2.0\",\"method\":\"" << method
        << "\",\"params\":" << params_json
        << ",\"id\":" << id << "}\n";
    return out.str();
}

int extractJsonRpcId(std::string_view response)
{
    auto pos = response.find("\"id\":");
    if (pos == std::string_view::npos) return -1;
    pos += 5;
    int id = 0;
    while (pos < response.size() && response[pos] >= '0' && response[pos] <= '9') {
        id = (id * 10) + (response[pos] - '0');
        ++pos;
    }
    return id;
}

bool isJsonRpcError(std::string_view response)
{
    return response.find("\"error\":") != std::string_view::npos;
}

std::string extractJsonRpcErrorMessage(std::string_view response)
{
    auto pos = response.find("\"message\":\"");
    if (pos == std::string_view::npos) return "Unknown error";
    pos += 11;
    std::string msg;
    while (pos < response.size() && response[pos] != '"') {
        if (response[pos] == '\\' && pos + 1 < response.size()) ++pos;
        msg += response[pos++];
    }
    return msg;
}

bool isReadyNotification(std::string_view line)
{
    return line.find("\"method\":\"ready\"") != std::string_view::npos
        || line.find("\"method\": \"ready\"") != std::string_view::npos;
}

#if defined(__linux__)
bool setNonBlocking(int fd)
{
    int flags = ::fcntl(fd, F_GETFL, 0);
    if (flags < 0) return false;
    return ::fcntl(fd, F_SETFL, flags | O_NONBLOCK) >= 0;
}

void closeIfOpen(int fd) noexcept
{
    if (fd >= 0) {
        ::close(fd);
    }
}

void closePipe(int fds[2]) noexcept
{
    closeIfOpen(fds[0]);
    closeIfOpen(fds[1]);
    fds[0] = -1;
    fds[1] = -1;
}
#endif

} // namespace

PluginRuntime::PluginRuntime(PluginRuntimeConfig config)
    : config_(std::move(config))
{
}

PluginRuntime::~PluginRuntime()
{
    stop();
}

PluginRuntime::PluginRuntime(PluginRuntime&& other) noexcept
{
    other.stop();
    config_ = std::move(other.config_);
    next_request_id_ = other.next_request_id_;
    stdout_buffer_ = std::move(other.stdout_buffer_);
    last_error_ = std::move(other.last_error_);
    restart_count_ = other.restart_count_;
    restart_window_start_ = other.restart_window_start_;
    healthy_ = other.healthy_.load(std::memory_order_acquire);
    other.pid_ = -1;
    other.stdin_fd_ = -1;
    other.stdout_fd_ = -1;
    other.stderr_fd_ = -1;
    other.healthy_ = false;
}

PluginRuntime& PluginRuntime::operator=(PluginRuntime&& other) noexcept
{
    if (this != &other) {
        stop();
        other.stop();
        config_ = std::move(other.config_);
        pid_ = other.pid_;
        stdin_fd_ = other.stdin_fd_;
        stdout_fd_ = other.stdout_fd_;
        stderr_fd_ = other.stderr_fd_;
        next_request_id_ = other.next_request_id_;
        stdout_buffer_ = std::move(other.stdout_buffer_);
        last_error_ = std::move(other.last_error_);
        restart_count_ = other.restart_count_;
        restart_window_start_ = other.restart_window_start_;
        healthy_ = other.healthy_.load(std::memory_order_acquire);
        other.pid_ = -1;
        other.stdin_fd_ = -1;
        other.stdout_fd_ = -1;
        other.stderr_fd_ = -1;
        other.healthy_ = false;
    }
    return *this;
}

#if defined(__linux__)
bool PluginRuntime::spawnProcess()
{
    int stdin_pipe[2]{-1, -1};
    int stdout_pipe[2]{-1, -1};
    int stderr_pipe[2]{-1, -1};

    if (::pipe(stdin_pipe) < 0 || ::pipe(stdout_pipe) < 0 || ::pipe(stderr_pipe) < 0) {
        last_error_ = "Failed to create pipes: " + std::string(std::strerror(errno));
        closePipe(stdin_pipe);
        closePipe(stdout_pipe);
        closePipe(stderr_pipe);
        return false;
    }

    pid_t child = ::fork();
    if (child < 0) {
        last_error_ = "Fork failed: " + std::string(std::strerror(errno));
        closePipe(stdin_pipe);
        closePipe(stdout_pipe);
        closePipe(stderr_pipe);
        return false;
    }

    if (child == 0) {
        ::dup2(stdin_pipe[0],  STDIN_FILENO);
        ::dup2(stdout_pipe[1], STDOUT_FILENO);
        ::dup2(stderr_pipe[1], STDERR_FILENO);

        ::close(stdin_pipe[0]);  ::close(stdin_pipe[1]);
        ::close(stdout_pipe[0]); ::close(stdout_pipe[1]);
        ::close(stderr_pipe[0]); ::close(stderr_pipe[1]);

        auto cwd = config_.cwd ? std::filesystem::path{*config_.cwd} : config_.plugin_dir;
        if (config_.cwd && cwd.is_relative()) {
            cwd = config_.plugin_dir / cwd;
        }
        if (!cwd.empty()) {
            std::error_code ec{};
            auto absolute_cwd = std::filesystem::absolute(cwd, ec);
            if (!ec) {
                ::chdir(absolute_cwd.string().c_str());
            }
        }

        for (const auto& [key, value] : config_.env) {
            ::setenv(key.c_str(), value.c_str(), 1);
        }

        std::vector<const char*> argv;
        argv.push_back(config_.command.c_str());
        for (const auto& arg : config_.args) {
            argv.push_back(arg.c_str());
        }
        argv.push_back(nullptr);

        ::execvp(config_.command.c_str(), const_cast<char* const*>(argv.data()));
        std::_Exit(127);
    }

    ::signal(SIGPIPE, SIG_IGN);

    ::close(stdin_pipe[0]);
    ::close(stdout_pipe[1]);
    ::close(stderr_pipe[1]);

    stdin_fd_  = stdin_pipe[1];
    stdout_fd_ = stdout_pipe[0];
    stderr_fd_ = stderr_pipe[0];
    pid_ = static_cast<int>(child);

    if (!setNonBlocking(stdout_fd_) || !setNonBlocking(stderr_fd_)) {
        last_error_ = "Failed to set plugin pipes non-blocking";
        killProcess();
        return false;
    }

    startStderrReader();
    if (!waitForReady()) {
        killProcess();
        return false;
    }

    healthy_ = true;
    return true;
}
#else
bool PluginRuntime::spawnProcess()
{
    last_error_ = "External helper plugins are not supported on this platform";
    healthy_ = false;
    return false;
}
#endif

bool PluginRuntime::waitForReady()
{
    auto line = readLine(config_.ready_timeout);
    if (!line) {
        last_error_ = "Plugin did not send ready within " + std::to_string(config_.ready_timeout.count()) + "ms";
        return false;
    }
    if (!isReadyNotification(*line)) {
        last_error_ = "Plugin startup returned non-ready message: " + *line;
        return false;
    }
    return true;
}

std::optional<std::string> PluginRuntime::readLine(std::chrono::milliseconds timeout)
{
#if defined(__linux__)
    const auto take_line = [this]() -> std::optional<std::string> {
        const auto nl = stdout_buffer_.find('\n');
        if (nl == std::string::npos) {
            return std::nullopt;
        }
        auto line = stdout_buffer_.substr(0, nl);
        stdout_buffer_.erase(0, nl + 1);
        return line;
    };
    if (auto line = take_line()) {
        return line;
    }

    stdout_buffer_.reserve(512);
    std::array<char, 1024> chunk{};
    auto deadline = std::chrono::steady_clock::now() + timeout;

    while (std::chrono::steady_clock::now() < deadline) {
        const auto remaining_ms = static_cast<int>(
            std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now()).count());
        struct pollfd pfd{};
        pfd.fd = stdout_fd_;
        pfd.events = POLLIN;

        int ret = ::poll(&pfd, 1, std::max(1, remaining_ms));
        if (ret < 0) {
            if (errno == EINTR) continue;
            last_error_ = "Poll error: " + std::string(std::strerror(errno));
            return std::nullopt;
        }
        if (ret == 0) break;

        ssize_t n = ::read(stdout_fd_, chunk.data(), chunk.size() - 1);
        if (n < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) continue;
            last_error_ = "Read error: " + std::string(std::strerror(errno));
            return std::nullopt;
        }
        if (n == 0) {
            last_error_ = "Plugin process exited unexpectedly";
            return std::nullopt;
        }

        stdout_buffer_.append(chunk.data(), static_cast<std::size_t>(n));
        if (stdout_buffer_.size() > static_cast<std::size_t>(kMaxResponseBytes)) {
            last_error_ = "Response too large";
            return std::nullopt;
        }
        if (auto line = take_line()) {
            return line;
        }
    }
#else
    (void)timeout;
#endif
    return std::nullopt;
}

bool PluginRuntime::writeAll(std::string_view data)
{
#if defined(__linux__)
    std::size_t written_total = 0;
    while (written_total < data.size()) {
        const auto* ptr = data.data() + written_total;
        const auto left = data.size() - written_total;
        ssize_t written = ::write(stdin_fd_, ptr, left);
        if (written < 0) {
            if (errno == EINTR) continue;
            last_error_ = "Failed to write request: " + std::string(std::strerror(errno));
            return false;
        }
        if (written == 0) {
            last_error_ = "Failed to write request: pipe closed";
            return false;
        }
        written_total += static_cast<std::size_t>(written);
    }
    return true;
#else
    (void)data;
    last_error_ = "External helper plugins are not supported on this platform";
    return false;
#endif
}

void PluginRuntime::startStderrReader()
{
#if defined(__linux__)
    if (stderr_fd_ < 0) return;

    auto log_root = config_.stderr_log_dir.empty() ? config_.plugin_dir : config_.stderr_log_dir;
    if (log_root.is_relative()) {
        log_root = config_.plugin_dir / log_root;
    }
    std::error_code ec{};
    std::filesystem::create_directories(log_root, ec);
    auto log_path = log_root / "stderr.log";

    stderr_running_ = true;
    stderr_thread_ = std::thread([this, log_path]() {
        std::ofstream log(log_path, std::ios::out | std::ios::app);
        std::array<char, 4096> buf{};
        while (stderr_running_) {
            struct pollfd pfd{};
            pfd.fd = stderr_fd_;
            pfd.events = POLLIN;
            int ret = ::poll(&pfd, 1, 250);
            if (ret < 0) {
                if (errno == EINTR) continue;
                break;
            }
            if (ret == 0) continue;

            ssize_t n = ::read(stderr_fd_, buf.data(), buf.size() - 1);
            if (n <= 0) break;
            if (log.is_open()) {
                buf[static_cast<std::size_t>(n)] = '\0';
                log << buf.data();
                log.flush();
            }
        }
    });
#endif
}

void PluginRuntime::stopStderrReader()
{
    stderr_running_ = false;
    if (stderr_thread_.joinable()) {
        stderr_thread_.join();
    }
}

void PluginRuntime::killProcess()
{
    stopStderrReader();

#if defined(__linux__)
    if (pid_ > 0) {
        ::kill(pid_, SIGTERM);
        int status = 0;
        ::waitpid(pid_, &status, 0);
        pid_ = -1;
    }
    if (stdin_fd_ >= 0) {
        ::close(stdin_fd_);
        stdin_fd_ = -1;
    }
    if (stdout_fd_ >= 0) {
        ::close(stdout_fd_);
        stdout_fd_ = -1;
    }
    if (stderr_fd_ >= 0) {
        ::close(stderr_fd_);
        stderr_fd_ = -1;
    }
#else
    pid_ = -1;
    stdin_fd_ = -1;
    stdout_fd_ = -1;
    stderr_fd_ = -1;
#endif
    stdout_buffer_.clear();
    healthy_ = false;
}

bool PluginRuntime::tryRestart()
{
    auto now = std::chrono::steady_clock::now();
    if (restart_window_start_.time_since_epoch().count() == 0
        || restart_window_start_ + std::chrono::minutes(1) < now) {
        restart_count_ = 0;
        restart_window_start_ = now;
    }
    if (restart_count_ >= config_.max_restarts_per_minute) {
        last_error_ = "Too many restarts (" + std::to_string(restart_count_) + " in last minute)";
        healthy_ = false;
        return false;
    }
    ++restart_count_;
    killProcess();
    return spawnProcess();
}

bool PluginRuntime::start()
{
    std::lock_guard<std::mutex> lock(call_mutex_);
    if (isRunning()) return true;
    return spawnProcess();
}

void PluginRuntime::stop()
{
    std::lock_guard<std::mutex> lock(call_mutex_);
    killProcess();
    restart_count_ = 0;
}

bool PluginRuntime::isRunning() const
{
    return pid_ > 0 && stdin_fd_ >= 0 && stdout_fd_ >= 0;
}

std::string PluginRuntime::readResponse(int request_id)
{
    auto deadline = std::chrono::steady_clock::now() + config_.request_timeout;

    while (std::chrono::steady_clock::now() < deadline) {
        const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
            deadline - std::chrono::steady_clock::now());
        auto line = readLine(std::max(std::chrono::milliseconds{1}, remaining));
        if (!line) {
            return {};
        }

        int response_id = extractJsonRpcId(*line);
        if (response_id != request_id) {
            continue;
        }
        if (isJsonRpcError(*line)) {
            last_error_ = "JSON-RPC error: " + extractJsonRpcErrorMessage(*line);
            healthy_ = false;
            return {};
        }
        healthy_ = true;
        return *line;
    }

    last_error_ = "Request timed out after " + std::to_string(config_.request_timeout.count()) + "ms";
    healthy_ = false;
    return {};
}

std::optional<std::string> PluginRuntime::call(
    std::string_view method,
    std::string_view params_json)
{
    std::lock_guard<std::mutex> lock(call_mutex_);

    if (!isRunning()) {
        killProcess();
        if (!spawnProcess()) {
            return std::nullopt;
        }
    }

    int id = next_request_id_++;
    std::string request = buildJsonRpcRequest(method, params_json, id);

    if (!writeAll(request)) {
        if (!tryRestart() || !writeAll(request)) {
            return std::nullopt;
        }
    }

    std::string response = readResponse(id);
    if (response.empty()) {
        return std::nullopt;
    }
    return response;
}

} // namespace lofibox::plugins
