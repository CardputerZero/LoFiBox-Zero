// SPDX-License-Identifier: GPL-3.0-or-later

#include "platform/host/runtime_host_internal.h"

#include <array>
#include <chrono>
#include <cctype>
#include <cstdlib>
#include <cerrno>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <wininet.h>
#pragma comment(lib, "wininet.lib")
#elif defined(__linux__)
#include <fcntl.h>
#include <csignal>
#include <spawn.h>
#include <sys/prctl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace fs = std::filesystem;

#if defined(__linux__)
extern char** environ;
#endif

namespace lofibox::platform::host::runtime_detail {

#if defined(_WIN32)
std::wstring readEnvWide(const wchar_t* name)
{
    const DWORD required = GetEnvironmentVariableW(name, nullptr, 0);
    if (required == 0) {
        return {};
    }

    std::wstring value(static_cast<std::size_t>(required), L'\0');
    const DWORD written = GetEnvironmentVariableW(name, value.data(), required);
    if (written == 0) {
        return {};
    }
    value.resize(static_cast<std::size_t>(written));
    return value;
}

std::optional<fs::path> resolveExecutablePath(const wchar_t* env_name, const wchar_t* executable_name)
{
    if (const auto env_path = readEnvWide(env_name); !env_path.empty() && fs::exists(env_path)) {
        return fs::path(env_path);
    }

    wchar_t resolved[MAX_PATH]{};
    if (SearchPathW(nullptr, executable_name, nullptr, MAX_PATH, resolved, nullptr) > 0) {
        return fs::path(resolved);
    }

    const auto local_app_data = readEnvWide(L"LOCALAPPDATA");
    if (!local_app_data.empty()) {
        const fs::path winget_link = fs::path(local_app_data) / "Microsoft" / "WinGet" / "Links" / executable_name;
        if (fs::exists(winget_link)) {
            return winget_link;
        }
    }

    return std::nullopt;
}

std::wstring utf8ToWide(std::string_view text)
{
    if (text.empty()) {
        return {};
    }

    const int required = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    if (required <= 0) {
        return {};
    }

    std::wstring result(static_cast<std::size_t>(required), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), result.data(), required);
    return result;
}

std::wstring quoteWindowsArg(std::string_view arg)
{
    std::wstring wide = utf8ToWide(arg);
    std::wstring quoted = L"\"";
    for (const wchar_t ch : wide) {
        if (ch == L'"') {
            quoted += L"\\\"";
        } else {
            quoted += ch;
        }
    }
    quoted += L"\"";
    return quoted;
}

std::wstring quoteWindowsPath(const fs::path& path)
{
    std::wstring quoted = L"\"";
    for (const wchar_t ch : path.wstring()) {
        if (ch == L'"') {
            quoted += L"\\\"";
        } else {
            quoted += ch;
        }
    }
    quoted += L"\"";
    return quoted;
}

std::wstring buildWindowsCommandLine(const fs::path& executable, const std::vector<std::string>& args)
{
    std::wstring command = quoteWindowsPath(executable);
    for (const auto& arg : args) {
        command += L" ";
        command += quoteWindowsArg(arg);
    }
    return command;
}

std::optional<std::string> captureProcessOutput(const fs::path& executable, const std::vector<std::string>& args)
{
    SECURITY_ATTRIBUTES security{};
    security.nLength = sizeof(security);
    security.bInheritHandle = TRUE;

    HANDLE read_handle = nullptr;
    HANDLE write_handle = nullptr;
    if (!CreatePipe(&read_handle, &write_handle, &security, 0)) {
        return std::nullopt;
    }
    SetHandleInformation(read_handle, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdOutput = write_handle;
    startup.hStdError = write_handle;

    PROCESS_INFORMATION process{};
    std::wstring command = buildWindowsCommandLine(executable, args);
    const BOOL created = CreateProcessW(
        executable.wstring().c_str(),
        command.data(),
        nullptr,
        nullptr,
        TRUE,
        CREATE_NO_WINDOW,
        nullptr,
        nullptr,
        &startup,
        &process);

    CloseHandle(write_handle);
    if (!created) {
        CloseHandle(read_handle);
        return std::nullopt;
    }

    std::string output{};
    std::array<char, 4096> buffer{};
    DWORD read_bytes = 0;
    while (ReadFile(read_handle, buffer.data(), static_cast<DWORD>(buffer.size()), &read_bytes, nullptr) && read_bytes > 0) {
        output.append(buffer.data(), buffer.data() + read_bytes);
    }

    WaitForSingleObject(process.hProcess, 10000);
    DWORD exit_code = 1;
    GetExitCodeProcess(process.hProcess, &exit_code);
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    CloseHandle(read_handle);
    if (exit_code != 0) {
        return std::nullopt;
    }
    return output;
}

bool runProcess(const fs::path& executable, const std::vector<std::string>& args)
{
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    std::wstring command = buildWindowsCommandLine(executable, args);
    const BOOL created = CreateProcessW(
        executable.wstring().c_str(),
        command.data(),
        nullptr,
        nullptr,
        FALSE,
        CREATE_NO_WINDOW,
        nullptr,
        nullptr,
        &startup,
        &process);
    if (!created) {
        return false;
    }
    WaitForSingleObject(process.hProcess, 10000);
    DWORD exit_code = 1;
    GetExitCodeProcess(process.hProcess, &exit_code);
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return exit_code == 0;
}

bool spawnAudioProcess(RunningProcess& process, const fs::path& executable, const std::vector<std::string>& args)
{
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    std::wstring command = buildWindowsCommandLine(executable, args);
    PROCESS_INFORMATION process_info{};
    const BOOL created = CreateProcessW(
        executable.wstring().c_str(),
        command.data(),
        nullptr,
        nullptr,
        FALSE,
        CREATE_NO_WINDOW,
        nullptr,
        nullptr,
        &startup,
        &process_info);
    if (!created) {
        return false;
    }
    process.process_info = process_info;
    process.active = true;
    return true;
}

void stopAudioProcess(RunningProcess& process)
{
    if (!process.active) {
        return;
    }
    DWORD exit_code = STILL_ACTIVE;
    if (GetExitCodeProcess(process.process_info.hProcess, &exit_code) && exit_code == STILL_ACTIVE) {
        TerminateProcess(process.process_info.hProcess, 0);
        WaitForSingleObject(process.process_info.hProcess, 1000);
    }
    if (process.process_info.hThread) {
        CloseHandle(process.process_info.hThread);
        process.process_info.hThread = nullptr;
    }
    if (process.process_info.hProcess) {
        CloseHandle(process.process_info.hProcess);
        process.process_info.hProcess = nullptr;
    }
    process.active = false;
}

void pauseAudioProcess(RunningProcess& process)
{
    if (!process.active || process.process_info.hThread == nullptr) {
        return;
    }
    SuspendThread(process.process_info.hThread);
}

void resumeAudioProcess(RunningProcess& process)
{
    if (!process.active || process.process_info.hThread == nullptr) {
        return;
    }
    while (ResumeThread(process.process_info.hThread) > 1) {
    }
}

bool audioProcessRunning(RunningProcess& process)
{
    if (!process.active) {
        return false;
    }
    DWORD exit_code = 0;
    if (!GetExitCodeProcess(process.process_info.hProcess, &exit_code)) {
        return false;
    }
    if (exit_code == STILL_ACTIVE) {
        return true;
    }
    stopAudioProcess(process);
    return false;
}

bool audioProcessFinished(RunningProcess& process)
{
    if (!process.active) {
        return false;
    }
    DWORD exit_code = 0;
    if (!GetExitCodeProcess(process.process_info.hProcess, &exit_code) || exit_code == STILL_ACTIVE) {
        return false;
    }
    stopAudioProcess(process);
    return true;
}

bool spawnPipeProcess(RunningPipeProcess& process, const fs::path& executable, const std::vector<std::string>& args)
{
    SECURITY_ATTRIBUTES security{};
    security.nLength = sizeof(security);
    security.bInheritHandle = TRUE;

    HANDLE read_handle = nullptr;
    HANDLE write_handle = nullptr;
    if (!CreatePipe(&read_handle, &write_handle, &security, 0)) {
        return false;
    }
    SetHandleInformation(read_handle, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdOutput = write_handle;
    startup.hStdError = GetStdHandle(STD_ERROR_HANDLE);
    startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);

    PROCESS_INFORMATION process_info{};
    std::wstring command = buildWindowsCommandLine(executable, args);
    const BOOL created = CreateProcessW(
        executable.wstring().c_str(),
        command.data(),
        nullptr,
        nullptr,
        TRUE,
        CREATE_NO_WINDOW,
        nullptr,
        nullptr,
        &startup,
        &process_info);
    CloseHandle(write_handle);
    if (!created) {
        CloseHandle(read_handle);
        return false;
    }

    process.process_info = process_info;
    process.read_handle = read_handle;
    process.active = true;
    return true;
}

int readPipeProcess(RunningPipeProcess& process, char* buffer, int max_bytes)
{
    if (!process.active || process.read_handle == nullptr || buffer == nullptr || max_bytes <= 0) {
        return 0;
    }
    DWORD read_bytes = 0;
    if (!ReadFile(process.read_handle, buffer, static_cast<DWORD>(max_bytes), &read_bytes, nullptr)) {
        return 0;
    }
    return static_cast<int>(read_bytes);
}

void stopPipeProcess(RunningPipeProcess& process)
{
    if (!process.active) {
        return;
    }
    DWORD exit_code = STILL_ACTIVE;
    if (GetExitCodeProcess(process.process_info.hProcess, &exit_code) && exit_code == STILL_ACTIVE) {
        TerminateProcess(process.process_info.hProcess, 0);
        WaitForSingleObject(process.process_info.hProcess, 1000);
    }
    if (process.read_handle) {
        CloseHandle(process.read_handle);
        process.read_handle = nullptr;
    }
    if (process.process_info.hThread) {
        CloseHandle(process.process_info.hThread);
        process.process_info.hThread = nullptr;
    }
    if (process.process_info.hProcess) {
        CloseHandle(process.process_info.hProcess);
        process.process_info.hProcess = nullptr;
    }
    process.active = false;
}

void pausePipeProcess(RunningPipeProcess& process)
{
    if (!process.active || process.process_info.hThread == nullptr) {
        return;
    }
    SuspendThread(process.process_info.hThread);
}

void resumePipeProcess(RunningPipeProcess& process)
{
    if (!process.active || process.process_info.hThread == nullptr) {
        return;
    }
    while (ResumeThread(process.process_info.hThread) > 1) {
    }
}

bool probeConnectivity()
{
    DWORD flags = 0;
    return InternetGetConnectedState(&flags, 0) == TRUE;
}

#elif defined(__linux__)
std::optional<fs::path> resolveExecutablePath(const char* env_name, const char* executable_name)
{
    if (env_name != nullptr) {
        if (const char* env_path = std::getenv(env_name); env_path != nullptr && *env_path != '\0' && fs::exists(env_path)) {
            return fs::path(env_path);
        }
    }

    const char* path_env = std::getenv("PATH");
    if (path_env == nullptr) {
        return std::nullopt;
    }

    std::string paths(path_env);
    std::size_t start = 0;
    while (start <= paths.size()) {
        const std::size_t end = paths.find(':', start);
        const std::string_view segment = end == std::string::npos
            ? std::string_view(paths).substr(start)
            : std::string_view(paths).substr(start, end - start);
        if (!segment.empty()) {
            fs::path candidate{std::string(segment)};
            candidate /= executable_name;
            std::error_code ec{};
            if (fs::exists(candidate, ec) && !ec) {
                return candidate;
            }
        }
        if (end == std::string::npos) {
            break;
        }
        start = end + 1;
    }

    return std::nullopt;
}

std::optional<std::string> captureProcessOutput(const fs::path& executable, const std::vector<std::string>& args)
{
    int pipe_fds[2]{-1, -1};
    if (pipe(pipe_fds) != 0) {
        return std::nullopt;
    }

    const pid_t child = fork();
    if (child < 0) {
        close(pipe_fds[0]);
        close(pipe_fds[1]);
        return std::nullopt;
    }

    if (child == 0) {
        dup2(pipe_fds[1], STDOUT_FILENO);
        dup2(pipe_fds[1], STDERR_FILENO);
        close(pipe_fds[0]);
        close(pipe_fds[1]);

        std::vector<std::string> owned_args{};
        owned_args.reserve(args.size() + 1);
        owned_args.push_back(executable.filename().string());
        owned_args.insert(owned_args.end(), args.begin(), args.end());

        std::vector<char*> argv{};
        argv.reserve(owned_args.size() + 1);
        for (auto& arg : owned_args) {
            argv.push_back(arg.data());
        }
        argv.push_back(nullptr);

        execv(executable.string().c_str(), argv.data());
        _exit(127);
    }

    close(pipe_fds[1]);
    std::string output{};
    std::array<char, 4096> buffer{};
    ssize_t read_bytes = 0;
    while ((read_bytes = read(pipe_fds[0], buffer.data(), buffer.size())) > 0) {
        output.append(buffer.data(), static_cast<std::size_t>(read_bytes));
    }
    close(pipe_fds[0]);

    int status = 0;
    waitpid(child, &status, 0);
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        return std::nullopt;
    }
    return output;
}

bool runProcess(const fs::path& executable, const std::vector<std::string>& args)
{
    const pid_t child = fork();
    if (child < 0) {
        return false;
    }
    if (child == 0) {
        const int devnull = open("/dev/null", O_WRONLY);
        if (devnull >= 0) {
            dup2(devnull, STDOUT_FILENO);
            dup2(devnull, STDERR_FILENO);
            close(devnull);
        }
        std::vector<std::string> owned_args{};
        owned_args.reserve(args.size() + 1);
        owned_args.push_back(executable.filename().string());
        owned_args.insert(owned_args.end(), args.begin(), args.end());

        std::vector<char*> argv{};
        argv.reserve(owned_args.size() + 1);
        for (auto& arg : owned_args) {
            argv.push_back(arg.data());
        }
        argv.push_back(nullptr);

        execv(executable.string().c_str(), argv.data());
        _exit(127);
    }

    int status = 0;
    waitpid(child, &status, 0);
    return WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

bool waitForChildExit(pid_t pid, std::chrono::milliseconds timeout)
{
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        int status = 0;
        const pid_t result = waitpid(pid, &status, WNOHANG);
        if (result == pid || result < 0) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds{20});
    }
    return false;
}

void signalProcessGroup(pid_t pid, int signal)
{
    if (pid <= 0) {
        return;
    }
    kill(-pid, signal);
    kill(pid, signal);
}

void terminateChildProcess(pid_t pid)
{
    if (pid <= 0) {
        return;
    }
    signalProcessGroup(pid, SIGTERM);
    if (waitForChildExit(pid, std::chrono::milliseconds{450})) {
        return;
    }
    signalProcessGroup(pid, SIGKILL);
    (void)waitForChildExit(pid, std::chrono::milliseconds{450});
}

void bindChildToParentDeath()
{
    (void)prctl(PR_SET_PDEATHSIG, SIGTERM);
    if (getppid() == 1) {
        _exit(128 + SIGTERM);
    }
}

bool spawnAudioProcess(RunningProcess& process, const fs::path& executable, const std::vector<std::string>& args)
{
    const pid_t child = fork();
    if (child < 0) {
        return false;
    }
    if (child == 0) {
        bindChildToParentDeath();
        setsid();
        std::vector<std::string> owned_args{};
        owned_args.reserve(args.size() + 1);
        owned_args.push_back(executable.filename().string());
        owned_args.insert(owned_args.end(), args.begin(), args.end());
        std::vector<char*> argv{};
        argv.reserve(owned_args.size() + 1);
        for (auto& arg : owned_args) {
            argv.push_back(arg.data());
        }
        argv.push_back(nullptr);
        execv(executable.string().c_str(), argv.data());
        _exit(127);
    }
    process.pid = child;
    process.active = true;
    return true;
}

void stopAudioProcess(RunningProcess& process)
{
    if (!process.active || process.pid <= 0) {
        return;
    }
    terminateChildProcess(process.pid);
    process.pid = -1;
    process.active = false;
}

void pauseAudioProcess(RunningProcess& process)
{
    if (!process.active || process.pid <= 0) {
        return;
    }
    signalProcessGroup(process.pid, SIGSTOP);
}

void resumeAudioProcess(RunningProcess& process)
{
    if (!process.active || process.pid <= 0) {
        return;
    }
    signalProcessGroup(process.pid, SIGCONT);
}

bool audioProcessRunning(RunningProcess& process)
{
    if (!process.active || process.pid <= 0) {
        return false;
    }
    int status = 0;
    const pid_t result = waitpid(process.pid, &status, WNOHANG);
    if (result == 0) {
        return true;
    }
    process.pid = -1;
    process.active = false;
    return false;
}

bool audioProcessFinished(RunningProcess& process)
{
    if (!process.active || process.pid <= 0) {
        return false;
    }
    int status = 0;
    const pid_t result = waitpid(process.pid, &status, WNOHANG);
    if (result == 0) {
        return false;
    }
    process.pid = -1;
    process.active = false;
    return true;
}

bool spawnPipeProcess(RunningPipeProcess& process, const fs::path& executable, const std::vector<std::string>& args)
{
    int pipe_fds[2]{-1, -1};
    if (pipe(pipe_fds) != 0) {
        return false;
    }

    std::vector<std::string> owned_args{};
    owned_args.reserve(args.size() + 1);
    owned_args.push_back(executable.filename().string());
    owned_args.insert(owned_args.end(), args.begin(), args.end());

    std::vector<char*> argv{};
    argv.reserve(owned_args.size() + 1);
    for (auto& arg : owned_args) {
        argv.push_back(arg.data());
    }
    argv.push_back(nullptr);

    posix_spawn_file_actions_t actions{};
    posix_spawnattr_t attributes{};
    if (posix_spawn_file_actions_init(&actions) != 0 || posix_spawnattr_init(&attributes) != 0) {
        close(pipe_fds[0]);
        close(pipe_fds[1]);
        return false;
    }

    short spawn_flags = 0;
#if defined(POSIX_SPAWN_SETSID)
    spawn_flags |= POSIX_SPAWN_SETSID;
#endif
    (void)posix_spawnattr_setflags(&attributes, spawn_flags);
    (void)posix_spawn_file_actions_adddup2(&actions, pipe_fds[1], STDOUT_FILENO);
    (void)posix_spawn_file_actions_addopen(&actions, STDERR_FILENO, "/dev/null", O_WRONLY, 0);
    (void)posix_spawn_file_actions_addclose(&actions, pipe_fds[0]);
    (void)posix_spawn_file_actions_addclose(&actions, pipe_fds[1]);

    pid_t child = -1;
    const int spawned = posix_spawn(&child, executable.string().c_str(), &actions, &attributes, argv.data(), ::environ);
    posix_spawn_file_actions_destroy(&actions);
    posix_spawnattr_destroy(&attributes);
    if (spawned != 0) {
        close(pipe_fds[0]);
        close(pipe_fds[1]);
        return false;
    }

    close(pipe_fds[1]);
    process.pid = child;
    process.read_fd = pipe_fds[0];
    const int flags = fcntl(process.read_fd, F_GETFL, 0);
    if (flags >= 0) {
        (void)fcntl(process.read_fd, F_SETFL, flags | O_NONBLOCK);
    }
    process.active = true;
    return true;
}

int readPipeProcess(RunningPipeProcess& process, char* buffer, int max_bytes)
{
    if (!process.active || process.read_fd < 0 || buffer == nullptr || max_bytes <= 0) {
        return 0;
    }
    const ssize_t bytes = read(process.read_fd, buffer, static_cast<std::size_t>(max_bytes));
    if (bytes < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
        return -1;
    }
    return bytes > 0 ? static_cast<int>(bytes) : 0;
}

void stopPipeProcess(RunningPipeProcess& process)
{
    if (!process.active) {
        return;
    }
    if (process.pid > 0) {
        terminateChildProcess(process.pid);
        process.pid = -1;
    }
    if (process.read_fd >= 0) {
        close(process.read_fd);
        process.read_fd = -1;
    }
    process.active = false;
}

void pausePipeProcess(RunningPipeProcess& process)
{
    if (!process.active || process.pid <= 0) {
        return;
    }
    kill(-process.pid, SIGSTOP);
    kill(process.pid, SIGSTOP);
}

void resumePipeProcess(RunningPipeProcess& process)
{
    if (!process.active || process.pid <= 0) {
        return;
    }
    kill(-process.pid, SIGCONT);
    kill(process.pid, SIGCONT);
}

bool pipeProcessRunning(RunningPipeProcess& process)
{
    if (!process.active || process.pid <= 0) {
        return false;
    }
    int status = 0;
    const pid_t result = waitpid(process.pid, &status, WNOHANG);
    if (result == 0) {
        return true;
    }
    process.pid = -1;
    process.active = false;
    if (process.read_fd >= 0) {
        close(process.read_fd);
        process.read_fd = -1;
    }
    return false;
}

bool spawnInputProcess(RunningInputProcess& process, const fs::path& executable, const std::vector<std::string>& args)
{
    int pipe_fds[2]{-1, -1};
    if (pipe(pipe_fds) != 0) {
        return false;
    }

    std::vector<std::string> owned_args{};
    owned_args.reserve(args.size() + 1);
    owned_args.push_back(executable.filename().string());
    owned_args.insert(owned_args.end(), args.begin(), args.end());

    std::vector<char*> argv{};
    argv.reserve(owned_args.size() + 1);
    for (auto& arg : owned_args) {
        argv.push_back(arg.data());
    }
    argv.push_back(nullptr);

    posix_spawn_file_actions_t actions{};
    posix_spawnattr_t attributes{};
    if (posix_spawn_file_actions_init(&actions) != 0 || posix_spawnattr_init(&attributes) != 0) {
        close(pipe_fds[0]);
        close(pipe_fds[1]);
        return false;
    }

    short spawn_flags = 0;
#if defined(POSIX_SPAWN_SETSID)
    spawn_flags |= POSIX_SPAWN_SETSID;
#endif
    (void)posix_spawnattr_setflags(&attributes, spawn_flags);
    (void)posix_spawn_file_actions_adddup2(&actions, pipe_fds[0], STDIN_FILENO);
    (void)posix_spawn_file_actions_addopen(&actions, STDOUT_FILENO, "/dev/null", O_WRONLY, 0);
    (void)posix_spawn_file_actions_addopen(&actions, STDERR_FILENO, "/dev/null", O_WRONLY, 0);
    (void)posix_spawn_file_actions_addclose(&actions, pipe_fds[0]);
    (void)posix_spawn_file_actions_addclose(&actions, pipe_fds[1]);

    pid_t child = -1;
    const int spawned = posix_spawn(&child, executable.string().c_str(), &actions, &attributes, argv.data(), ::environ);
    posix_spawn_file_actions_destroy(&actions);
    posix_spawnattr_destroy(&attributes);
    if (spawned != 0) {
        close(pipe_fds[0]);
        close(pipe_fds[1]);
        return false;
    }

    close(pipe_fds[0]);
    process.pid = child;
    process.write_fd = pipe_fds[1];
    process.active = true;
    return true;
}

int writeInputProcess(RunningInputProcess& process, const char* buffer, int max_bytes)
{
    static const bool sigpipe_ignored = [] {
        std::signal(SIGPIPE, SIG_IGN);
        return true;
    }();
    (void)sigpipe_ignored;
    if (!process.active || process.write_fd < 0 || buffer == nullptr || max_bytes <= 0) {
        return 0;
    }
    while (true) {
        const ssize_t bytes = write(process.write_fd, buffer, static_cast<std::size_t>(max_bytes));
        if (bytes < 0 && errno == EINTR) {
            continue;
        }
        if (bytes < 0 && (errno == EPIPE || errno == EBADF)) {
            return 0;
        }
        return bytes > 0 ? static_cast<int>(bytes) : 0;
    }
}

void closeInputProcess(RunningInputProcess& process)
{
    if (process.write_fd >= 0) {
        close(process.write_fd);
        process.write_fd = -1;
    }
}

void stopInputProcess(RunningInputProcess& process)
{
    closeInputProcess(process);
    if (!process.active) {
        return;
    }
    if (process.pid > 0) {
        terminateChildProcess(process.pid);
        process.pid = -1;
    }
    process.active = false;
}

void pauseInputProcess(RunningInputProcess& process)
{
    if (!process.active || process.pid <= 0) {
        return;
    }
    kill(-process.pid, SIGSTOP);
    kill(process.pid, SIGSTOP);
}

void resumeInputProcess(RunningInputProcess& process)
{
    if (!process.active || process.pid <= 0) {
        return;
    }
    kill(-process.pid, SIGCONT);
    kill(process.pid, SIGCONT);
}

bool inputProcessRunning(RunningInputProcess& process)
{
    if (!process.active || process.pid <= 0) {
        return false;
    }
    int status = 0;
    const pid_t result = waitpid(process.pid, &status, WNOHANG);
    if (result == 0) {
        return true;
    }
    process.pid = -1;
    process.active = false;
    closeInputProcess(process);
    return false;
}

bool inputProcessFinished(RunningInputProcess& process)
{
    if (!process.active || process.pid <= 0) {
        return false;
    }
    int status = 0;
    const pid_t result = waitpid(process.pid, &status, WNOHANG);
    if (result == 0) {
        return false;
    }
    process.pid = -1;
    process.active = false;
    closeInputProcess(process);
    return true;
}

bool probeConnectivity()
{
    std::ifstream route_file("/proc/net/route");
    if (!route_file.is_open()) {
        return false;
    }

    std::string line{};
    std::getline(route_file, line);
    while (std::getline(route_file, line)) {
        if (line.empty()) {
            continue;
        }
        std::vector<std::string> fields{};
        std::size_t start = 0;
        while (start < line.size()) {
            while (start < line.size() && std::isspace(static_cast<unsigned char>(line[start])) != 0) {
                ++start;
            }
            std::size_t end = start;
            while (end < line.size() && std::isspace(static_cast<unsigned char>(line[end])) == 0) {
                ++end;
            }
            if (end > start) {
                fields.emplace_back(line.substr(start, end - start));
            }
            start = end;
        }

        if (fields.size() >= 3 && fields[1] == "00000000") {
            return true;
        }
    }

    return false;
}
#endif

} // namespace lofibox::platform::host::runtime_detail
