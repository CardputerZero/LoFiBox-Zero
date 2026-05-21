// SPDX-License-Identifier: GPL-3.0-or-later

#include "platform/host/runtime_provider_factories.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#if defined(__linux__)
#include <unistd.h>
#endif

#include "audio/dsp/realtime_dsp_engine.h"
#include "platform/host/png_canvas_loader.h"
#include "platform/host/runtime_logger.h"

namespace fs = std::filesystem;

namespace lofibox::platform::host {
namespace {
using namespace runtime_detail;

struct AudioExecutable {
    fs::path path{};
    bool windows_interop{false};
};

enum class PcmOutputSinkKind {
    Aplay,
    Ffplay,
    PipeWireCat,
};

struct PcmOutputSink {
    fs::path path{};
    PcmOutputSinkKind kind{PcmOutputSinkKind::Ffplay};
};

#if defined(__linux__)
bool runningUnderWsl()
{
    std::ifstream input("/proc/sys/kernel/osrelease");
    std::string value{};
    std::getline(input, value);
    for (char& ch : value) {
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    }
    return value.find("microsoft") != std::string::npos || value.find("wsl") != std::string::npos;
}

std::optional<fs::path> resolveExecutableFromPath(const char* executable_name)
{
    return resolveExecutablePath(nullptr, executable_name);
}

bool isWindowsExecutablePath(const fs::path& path)
{
    auto extension = path.extension().string();
    for (char& ch : extension) {
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    }
    return extension == ".exe";
}

std::string wslPathForWindowsProcess(const fs::path& path)
{
    std::string input = pathUtf8String(path);
    if (input.rfind("/mnt/", 0) == 0 && input.size() > 6 && input[6] == '/') {
        const char drive = input[5];
        if (std::isalpha(static_cast<unsigned char>(drive)) != 0) {
            std::string output{};
            output.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(drive))));
            output += ':';
            for (std::size_t i = 6; i < input.size(); ++i) {
                output.push_back(input[i] == '/' ? '\\' : input[i]);
            }
            return output;
        }
    }

    if (!input.empty() && input.front() == '/') {
        const char* distro = std::getenv("WSL_DISTRO_NAME");
        if (distro != nullptr && *distro != '\0') {
            std::string output = "\\\\wsl.localhost\\";
            output += distro;
            for (const char ch : input) {
                output.push_back(ch == '/' ? '\\' : ch);
            }
            return output;
        }
    }

    return input;
}
#endif

AudioExecutable resolveAudioExecutable()
{
#if defined(_WIN32)
    if (auto path = resolveExecutablePath(L"FFPLAY_PATH", L"ffplay.exe")) {
        return AudioExecutable{*path, false};
    }
#elif defined(__linux__)
    if (const char* env_path = std::getenv("FFPLAY_PATH"); env_path != nullptr && *env_path != '\0') {
        fs::path path{env_path};
        if (fs::exists(path)) {
            return AudioExecutable{path, isWindowsExecutablePath(path)};
        }
    }

    if (auto path = resolveExecutableFromPath("ffplay")) {
        return AudioExecutable{*path, false};
    }

    if (runningUnderWsl()) {
        if (auto windows_ffplay = resolveExecutableFromPath("ffplay.exe")) {
            logRuntime(RuntimeLogLevel::Info, "audio", "Using Windows ffplay fallback for WSL audio");
            return AudioExecutable{*windows_ffplay, true};
        }
    }
#endif
    return {};
}

AudioExecutable resolveAnalyzerExecutable()
{
#if defined(_WIN32)
    if (auto path = resolveExecutablePath(L"FFMPEG_PATH", L"ffmpeg.exe")) {
        return AudioExecutable{*path, false};
    }
#elif defined(__linux__)
    if (const char* env_path = std::getenv("FFMPEG_PATH"); env_path != nullptr && *env_path != '\0') {
        fs::path path{env_path};
        if (fs::exists(path)) {
            return AudioExecutable{path, isWindowsExecutablePath(path)};
        }
    }

    if (auto path = resolveExecutableFromPath("ffmpeg")) {
        return AudioExecutable{*path, false};
    }

    if (runningUnderWsl()) {
        if (auto windows_ffmpeg = resolveExecutableFromPath("ffmpeg.exe")) {
            logRuntime(RuntimeLogLevel::Info, "audio", "Using Windows ffmpeg fallback for WSL visualization");
            return AudioExecutable{*windows_ffmpeg, true};
        }
    }
#endif
    return {};
}

#if defined(__linux__)
bool pipeWireRuntimeAvailable()
{
    std::vector<fs::path> candidates{};
    if (const char* runtime_dir = std::getenv("PIPEWIRE_RUNTIME_DIR"); runtime_dir != nullptr && *runtime_dir != '\0') {
        candidates.emplace_back(runtime_dir);
    }
    if (const char* runtime_dir = std::getenv("XDG_RUNTIME_DIR"); runtime_dir != nullptr && *runtime_dir != '\0') {
        candidates.emplace_back(runtime_dir);
    }
    candidates.emplace_back(fs::path{"/run/user"} / std::to_string(static_cast<long long>(getuid())));

    for (const auto& candidate : candidates) {
        std::error_code ec{};
        if (!candidate.empty() && fs::exists(candidate / "pipewire-0", ec)) {
            return true;
        }
    }
    return false;
}

PcmOutputSink resolvePcmOutputSink(const AudioExecutable& ffplay)
{
    if (pipeWireRuntimeAvailable()) {
        if (auto path = resolveExecutableFromPath("pw-cat")) {
            return PcmOutputSink{*path, PcmOutputSinkKind::PipeWireCat};
        }
    }

    if (auto path = resolveExecutableFromPath("aplay")) {
        return PcmOutputSink{*path, PcmOutputSinkKind::Aplay};
    }

    if (auto path = resolveExecutableFromPath("pw-cat")) {
        return PcmOutputSink{*path, PcmOutputSinkKind::PipeWireCat};
    }

    if (!ffplay.path.empty() && !ffplay.windows_interop) {
        return PcmOutputSink{ffplay.path, PcmOutputSinkKind::Ffplay};
    }

    return {};
}
#endif

bool networkInput(std::string_view input) noexcept
{
    return input.find("://") != std::string_view::npos;
}

constexpr std::chrono::seconds kNetworkPlaybackUiStartDelay{24};
constexpr std::chrono::seconds kNetworkAnalyzerWarmupLimit{5};

app::AudioVisualizationFrame spectrumFrameFromPcmWindow(const std::vector<float>& samples, double sample_rate)
{
    constexpr double kPi = 3.14159265358979323846;
    constexpr std::array<double, 10> kBandCenters{
        63.0,
        125.0,
        250.0,
        500.0,
        1000.0,
        2000.0,
        3000.0,
        4000.0,
        5500.0,
        7000.0,
    };

    app::AudioVisualizationFrame frame{};
    if (samples.empty()) {
        return frame;
    }

    frame.available = true;
    const double n = static_cast<double>(samples.size());
    for (std::size_t band = 0; band < kBandCenters.size(); ++band) {
        const double k = std::round((n * kBandCenters[band]) / sample_rate);
        const double omega = (2.0 * kPi * k) / n;
        const double coeff = 2.0 * std::cos(omega);
        double q0 = 0.0;
        double q1 = 0.0;
        double q2 = 0.0;
        for (std::size_t index = 0; index < samples.size(); ++index) {
            const double window = 0.5 - (0.5 * std::cos((2.0 * kPi * static_cast<double>(index)) / std::max(1.0, n - 1.0)));
            q0 = (coeff * q1) - q2 + (static_cast<double>(samples[index]) * window);
            q2 = q1;
            q1 = q0;
        }
        const double magnitude = std::sqrt(std::max(0.0, (q1 * q1) + (q2 * q2) - (q1 * q2 * coeff))) / n;
        const double compressed = std::pow(std::log1p(magnitude * 96.0) / std::log1p(96.0), 0.68);
        frame.bands[band] = static_cast<float>(std::clamp(compressed, 0.0, 1.0));
    }
    return frame;
}

class RealtimePcmSpectrumAnalyzer {
public:
    ~RealtimePcmSpectrumAnalyzer()
    {
        stop();
    }

    void start(const AudioExecutable& executable, const std::string& input, double start_seconds)
    {
        stop();
        {
            std::lock_guard lock(frame_mutex_);
            frame_ = {};
        }
        if (executable.path.empty() || input.empty()) {
            logRuntime(RuntimeLogLevel::Debug, "audio", "Visualization unavailable: ffmpeg not found");
            return;
        }

        std::vector<std::string> args{
            "-hide_banner",
            "-loglevel",
            "error",
            "-nostdin",
            "-re"};
        if (networkInput(input)) {
            args.emplace_back("-fflags");
            args.emplace_back("nobuffer");
            args.emplace_back("-flags");
            args.emplace_back("low_delay");
            args.emplace_back("-probesize");
            args.emplace_back("32768");
            args.emplace_back("-analyzeduration");
            args.emplace_back("0");
        }
        if (start_seconds > 0.0) {
            const auto seek_ms = static_cast<int>(std::floor(start_seconds * 1000.0));
            const int whole = seek_ms / 1000;
            const int frac = std::abs(seek_ms % 1000);
            char buffer[32]{};
            std::snprintf(buffer, sizeof(buffer), "%d.%03d", whole, frac);
            args.emplace_back("-ss");
            args.emplace_back(buffer);
        }
        args.emplace_back("-i");
        args.emplace_back(input);
        args.emplace_back("-vn");
        args.emplace_back("-ac");
        args.emplace_back("1");
        args.emplace_back("-ar");
        args.emplace_back("16000");
        args.emplace_back("-f");
        args.emplace_back("s16le");
        args.emplace_back("-");

        stop_requested_ = false;
        worker_ = std::thread([this, executable_path = executable.path, args = std::move(args)]() mutable {
            run(executable_path, std::move(args));
        });
    }

    void pause() noexcept
    {
        pausePipeProcess(process_);
    }

    void resume() noexcept
    {
        resumePipeProcess(process_);
    }

    void stop()
    {
        stop_requested_ = true;
        stopPipeProcess(process_);
        if (worker_.joinable()) {
            worker_.join();
        }
        {
            std::lock_guard lock(frame_mutex_);
            frame_ = {};
        }
        stop_requested_ = false;
    }

    [[nodiscard]] app::AudioVisualizationFrame currentFrame() const
    {
        std::lock_guard lock(frame_mutex_);
        return frame_;
    }

private:
    void run(const fs::path& executable, std::vector<std::string> args)
    {
        if (!spawnPipeProcess(process_, executable, args)) {
            return;
        }

        std::array<char, 4096> buffer{};
        std::vector<float> window{};
        window.reserve(kWindowSize);
        std::optional<unsigned char> carry{};
        while (!stop_requested_) {
            const int read_bytes = readPipeProcess(process_, buffer.data(), static_cast<int>(buffer.size()));
            if (read_bytes < 0) {
                std::this_thread::sleep_for(std::chrono::milliseconds{10});
                continue;
            }
            if (read_bytes == 0) {
                break;
            }

            int offset = 0;
            if (carry && read_bytes > 0) {
                const auto low = static_cast<unsigned int>(*carry);
                const auto high = static_cast<unsigned int>(static_cast<unsigned char>(buffer[0]));
                appendSample(window, static_cast<std::int16_t>(low | (high << 8U)));
                offset = 1;
                carry.reset();
            }

            for (; offset + 1 < read_bytes; offset += 2) {
                const auto low = static_cast<unsigned int>(static_cast<unsigned char>(buffer[static_cast<std::size_t>(offset)]));
                const auto high = static_cast<unsigned int>(static_cast<unsigned char>(buffer[static_cast<std::size_t>(offset + 1)]));
                appendSample(window, static_cast<std::int16_t>(low | (high << 8U)));
            }
            if (offset < read_bytes) {
                carry = static_cast<unsigned char>(buffer[static_cast<std::size_t>(offset)]);
            }
        }
        stopPipeProcess(process_);
    }

    void appendSample(std::vector<float>& window, std::int16_t sample)
    {
        window.push_back(static_cast<float>(sample) / 32768.0f);
        if (window.size() < kWindowSize) {
            return;
        }
        publishFrame(spectrumFrameFromPcmWindow(window, 16000.0));
        window.clear();
    }

    void publishFrame(app::AudioVisualizationFrame frame)
    {
        std::lock_guard lock(frame_mutex_);
        if (frame_.available) {
            for (std::size_t index = 0; index < frame.bands.size(); ++index) {
                frame.bands[index] = (frame_.bands[index] * 0.58f) + (frame.bands[index] * 0.42f);
            }
        }
        frame_ = frame;
    }

    static constexpr std::size_t kWindowSize{1024};

    mutable std::mutex frame_mutex_{};
    app::AudioVisualizationFrame frame_{};
    std::thread worker_{};
    std::atomic_bool stop_requested_{false};
    RunningPipeProcess process_{};
};

#if defined(__linux__)
class RealtimePcmPlaybackPipeline {
public:
    ~RealtimePcmPlaybackPipeline()
    {
        stop();
    }

    void setDspProfile(const audio::dsp::DspChainProfile& profile)
    {
        dsp_.setProfile(profile);
    }

    [[nodiscard]] bool start(
        const AudioExecutable& decoder,
        const PcmOutputSink& sink,
        const std::string& input,
        double start_seconds,
        const audio::dsp::DspChainProfile& profile)
    {
        stop();
        return startWithoutStoppingCurrent(decoder, sink, input, start_seconds, profile);
    }

    [[nodiscard]] bool startWithoutStoppingCurrent(
        const AudioExecutable& decoder,
        const PcmOutputSink& sink,
        const std::string& input,
        double start_seconds,
        const audio::dsp::DspChainProfile& profile)
    {
        if (decoder.path.empty() || sink.path.empty() || input.empty()) {
            return false;
        }

        dsp_.reset(kSampleRate, kChannels);
        dsp_.setProfile(profile);
        {
            std::lock_guard lock(frame_mutex_);
            frame_ = {};
        }
        {
            std::lock_guard lock(state_mutex_);
            started_ = false;
            finished_ = false;
            failed_ = false;
            paused_ = false;
            input_is_network_ = networkInput(input);
            sink_kind_ = sink.kind;
            first_output_write_at_ = {};
            last_output_write_at_ = {};
            position_base_seconds_ = std::max(0.0, start_seconds);
            frames_written_ = 0U;
        }

        std::vector<std::string> sink_args{};
        if (sink.kind == PcmOutputSinkKind::Aplay) {
            sink_args = {
                "-q",
                "-t",
                "raw",
                "-f",
                "FLOAT_LE",
                "-r",
                std::to_string(kSampleRate),
                "-c",
                std::to_string(kChannels),
                "--buffer-time=70000",
                "--period-time=15000",
                "-",
            };
        } else if (sink.kind == PcmOutputSinkKind::PipeWireCat) {
            sink_args = {
                "--playback",
                "--raw",
                "--media-role",
                "Music",
                "--latency",
                "45ms",
                "--rate",
                std::to_string(kSampleRate),
                "--channels",
                std::to_string(kChannels),
                "--channel-map",
                "stereo",
                "--format",
                "f32",
                "-",
            };
        } else {
            sink_args = {
                "-nodisp",
                "-autoexit",
                "-fflags",
                "nobuffer",
                "-flags",
                "low_delay",
                "-probesize",
                "32",
                "-analyzeduration",
                "0",
                "-sync",
                "audio",
                "-loglevel",
                "warning",
                "-nostats",
                "-f",
                "f32le",
                "-ar",
                std::to_string(kSampleRate),
                "-ch_layout",
                "stereo",
                "-i",
                "-",
            };
        }
        if (!spawnInputProcess(sink_process_, sink.path, sink_args)) {
            markFailed();
            return false;
        }

        const bool is_network = networkInput(input);
        std::vector<std::string> decoder_args{
            "-hide_banner",
            "-loglevel",
            "error",
            "-nostdin",
            "-re",
        };
        if (is_network) {
            decoder_args.emplace_back("-fflags");
            decoder_args.emplace_back("nobuffer");
            decoder_args.emplace_back("-flags");
            decoder_args.emplace_back("low_delay");
            decoder_args.emplace_back("-probesize");
            decoder_args.emplace_back("32768");
            decoder_args.emplace_back("-analyzeduration");
            decoder_args.emplace_back("0");
        }
        if (start_seconds > 0.0) {
            const auto seek_ms = static_cast<int>(std::floor(start_seconds * 1000.0));
            const int whole = seek_ms / 1000;
            const int frac = std::abs(seek_ms % 1000);
            char buffer[32]{};
            std::snprintf(buffer, sizeof(buffer), "%d.%03d", whole, frac);
            decoder_args.emplace_back("-ss");
            decoder_args.emplace_back(buffer);
        }
        decoder_args.emplace_back("-i");
        decoder_args.emplace_back(input);
        decoder_args.emplace_back("-vn");
        decoder_args.emplace_back("-ac");
        decoder_args.emplace_back(std::to_string(kChannels));
        decoder_args.emplace_back("-ar");
        decoder_args.emplace_back(std::to_string(kSampleRate));
        decoder_args.emplace_back("-f");
        decoder_args.emplace_back("f32le");
        decoder_args.emplace_back("-");

        if (!spawnPipeProcess(decoder_process_, decoder.path, decoder_args)) {
            stopInputProcess(sink_process_);
            markFailed();
            return false;
        }

        stop_requested_ = false;
        worker_done_ = false;
        worker_ = std::thread([this]() {
            run();
        });
        return true;
    }

    void pause()
    {
        {
            std::lock_guard lock(state_mutex_);
            if (paused_ || finished_ || failed_) {
                return;
            }
            paused_ = true;
        }
        pausePipeProcess(decoder_process_);
        pauseInputProcess(sink_process_);
    }

    void resume()
    {
        {
            std::lock_guard lock(state_mutex_);
            if (!paused_ || finished_ || failed_) {
                return;
            }
            paused_ = false;
        }
        resumeInputProcess(sink_process_);
        resumePipeProcess(decoder_process_);
    }

    void stop()
    {
        requestStop();
        waitForStop();
    }

    void requestStop()
    {
        stop_requested_ = true;
        stopPipeProcess(decoder_process_);
        stopInputProcess(sink_process_);
    }

    void waitForStop()
    {
        if (worker_.joinable()) {
            worker_.join();
        }
        worker_done_ = false;
        {
            std::lock_guard lock(state_mutex_);
            started_ = false;
            finished_ = false;
            failed_ = false;
            paused_ = false;
            input_is_network_ = false;
            sink_kind_ = PcmOutputSinkKind::Ffplay;
            first_output_write_at_ = {};
            last_output_write_at_ = {};
            position_base_seconds_ = 0.0;
            frames_written_ = 0U;
        }
        {
            std::lock_guard lock(frame_mutex_);
            frame_ = {};
            visualization_window_.clear();
        }
        stop_requested_ = false;
    }

    [[nodiscard]] app::AudioPlaybackState state()
    {
        std::lock_guard lock(state_mutex_);
        if (failed_) {
            return app::AudioPlaybackState::Failed;
        }
        if (finished_) {
            return app::AudioPlaybackState::Finished;
        }
        if (paused_) {
            return app::AudioPlaybackState::Paused;
        }
        if (!started_ && outputStartConfirmedLocked(std::chrono::steady_clock::now())) {
            started_ = true;
            logRuntime(
                RuntimeLogLevel::Debug,
                "audio",
                "Confirmed realtime PCM output start after "
                    + std::to_string(outputStartDelayMsLocked())
                    + " ms and "
                    + std::to_string(frames_written_)
                    + " frames via "
                    + sinkNameLocked());
        }
        if (!started_) {
            return app::AudioPlaybackState::Starting;
        }
        return app::AudioPlaybackState::Playing;
    }

    [[nodiscard]] bool running()
    {
        const auto current = state();
        return current == app::AudioPlaybackState::Starting || current == app::AudioPlaybackState::Playing;
    }

    [[nodiscard]] bool finished()
    {
        return state() == app::AudioPlaybackState::Finished;
    }

    [[nodiscard]] bool stopped() const
    {
        return !worker_.joinable() || worker_done_;
    }

    [[nodiscard]] app::AudioVisualizationFrame visualizationFrame() const
    {
        std::lock_guard lock(frame_mutex_);
        return frame_;
    }

    [[nodiscard]] std::optional<double> positionSeconds() const
    {
        std::lock_guard lock(state_mutex_);
        if (frames_written_ == 0U) {
            return std::nullopt;
        }

        const double seconds_written = static_cast<double>(frames_written_) / static_cast<double>(kSampleRate);
        return position_base_seconds_ + std::max(0.0, seconds_written - estimatedOutputLatencySecondsLocked());
    }

private:
    void run()
    {
        struct WorkerDoneGuard {
            std::atomic_bool& done;
            ~WorkerDoneGuard()
            {
                done = true;
            }
        } worker_done_guard{worker_done_};

        std::array<char, 8192> buffer{};
        std::vector<char> carry{};
        carry.reserve(kFrameBytes);
        while (!stop_requested_) {
            const int read_bytes = readPipeProcess(decoder_process_, buffer.data(), static_cast<int>(buffer.size()));
            if (read_bytes < 0) {
                if (!pipeProcessRunning(decoder_process_)) {
                    break;
                }
                if (!inputProcessRunning(sink_process_)) {
                    markFailed("Realtime PCM sink exited while decoder output was pending");
                    break;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds{5});
                continue;
            }
            if (read_bytes == 0) {
                break;
            }
            carry.insert(carry.end(), buffer.begin(), buffer.begin() + read_bytes);
            processCarry(carry);
        }

        if (!stop_requested_) {
            if (!hasWrittenOutput()) {
                closeInputProcess(sink_process_);
                stopPipeProcess(decoder_process_);
                stopInputProcess(sink_process_);
                markFailed("Realtime PCM decoder ended before output was confirmed");
                return;
            }
            closeInputProcess(sink_process_);
            for (int attempts = 0; attempts < 120 && inputProcessRunning(sink_process_); ++attempts) {
                std::this_thread::sleep_for(std::chrono::milliseconds{25});
            }
        }
        stopPipeProcess(decoder_process_);
        stopInputProcess(sink_process_);
        if (!stop_requested_) {
            std::lock_guard lock(state_mutex_);
            finished_ = !failed_;
            paused_ = false;
        }
    }

    void processCarry(std::vector<char>& carry)
    {
        const std::size_t full_bytes = carry.size() - (carry.size() % kFrameBytes);
        if (full_bytes == 0U) {
            return;
        }

        std::vector<float> samples(full_bytes / sizeof(float));
        std::memcpy(samples.data(), carry.data(), full_bytes);
        const std::size_t frame_count = samples.size() / static_cast<std::size_t>(kChannels);
        dsp_.processInterleaved(samples.data(), frame_count, kChannels, kSampleRate);
        publishVisualization(samples);

        clip_stats_counter_ += frame_count;
        if (clip_stats_counter_ >= static_cast<std::size_t>(kSampleRate) * 5) {
            clip_stats_counter_ = 0;
            const auto stats = dsp_.clipStats();
            if (stats.over_ceiling_count > 0 || stats.over_fullscale_count > 0) {
                logRuntime(
                    RuntimeLogLevel::Debug,
                    "audio",
                    "DSP clip: ceiling_clips=" + std::to_string(stats.over_ceiling_count)
                        + " fullscale_clips=" + std::to_string(stats.over_fullscale_count)
                        + " peak_before=" + std::to_string(stats.peak_before)
                        + " peak_after=" + std::to_string(stats.peak_after));
            }
            dsp_.resetClipStats();
        }

        if (!writeAll(reinterpret_cast<const char*>(samples.data()), static_cast<int>(samples.size() * sizeof(float)))) {
            markFailed();
            return;
        }
        rememberOutputWrite(frame_count);
        carry.erase(carry.begin(), carry.begin() + static_cast<std::ptrdiff_t>(full_bytes));
    }

    bool writeAll(const char* data, int bytes)
    {
        int written = 0;
        while (written < bytes && !stop_requested_) {
            const int result = writeInputProcess(sink_process_, data + written, bytes - written);
            if (result <= 0) {
                return false;
            }
            written += result;
        }
        return written == bytes;
    }

    void publishVisualization(const std::vector<float>& interleaved_samples)
    {
        std::lock_guard lock(frame_mutex_);
        for (std::size_t index = 0; index + 1U < interleaved_samples.size(); index += static_cast<std::size_t>(kChannels)) {
            const float mono = (interleaved_samples[index] + interleaved_samples[index + 1U]) * 0.5f;
            visualization_window_.push_back(mono);
            if (visualization_window_.size() >= kVisualizationWindowSize) {
                const auto next_frame = spectrumFrameFromPcmWindow(visualization_window_, static_cast<double>(kSampleRate));
                if (frame_.available) {
                    for (std::size_t band = 0; band < next_frame.bands.size(); ++band) {
                        frame_.bands[band] = (frame_.bands[band] * 0.45f) + (next_frame.bands[band] * 0.55f);
                    }
                    frame_.available = true;
                } else {
                    frame_ = next_frame;
                }
                visualization_window_.clear();
            }
        }
    }

    void rememberOutputWrite(std::size_t frame_count)
    {
        std::lock_guard lock(state_mutex_);
        const auto now = std::chrono::steady_clock::now();
        if (first_output_write_at_ == std::chrono::steady_clock::time_point{}) {
            first_output_write_at_ = now;
            logRuntime(RuntimeLogLevel::Debug, "audio", "Realtime PCM first processed frame written to output pipe");
        }
        last_output_write_at_ = now;
        frames_written_ += frame_count;
    }

    [[nodiscard]] double estimatedOutputLatencySecondsLocked() const noexcept
    {
        if (sink_kind_ == PcmOutputSinkKind::Aplay) {
            return input_is_network_ ? 0.25 : 0.12;
        }
        if (sink_kind_ == PcmOutputSinkKind::PipeWireCat) {
            return input_is_network_ ? 0.25 : 0.12;
        }
        return input_is_network_ ? 0.90 : 0.45;
    }

    [[nodiscard]] bool hasWrittenOutput() const
    {
        std::lock_guard lock(state_mutex_);
        return first_output_write_at_ != std::chrono::steady_clock::time_point{};
    }

    [[nodiscard]] bool outputStartConfirmedLocked(std::chrono::steady_clock::time_point now) const
    {
        if (first_output_write_at_ == std::chrono::steady_clock::time_point{}) {
            return false;
        }
        const auto elapsed_since_first_write = now - first_output_write_at_;
        const auto elapsed_writing = last_output_write_at_ - first_output_write_at_;
        const double seconds_written = static_cast<double>(frames_written_) / static_cast<double>(kSampleRate);
        const bool low_latency_sink = sink_kind_ == PcmOutputSinkKind::Aplay || sink_kind_ == PcmOutputSinkKind::PipeWireCat;
        const auto minimum_output_delay = low_latency_sink
            ? (input_is_network_ ? std::chrono::milliseconds{900} : std::chrono::milliseconds{350})
            : (input_is_network_ ? std::chrono::seconds{3} : std::chrono::milliseconds{700});
        const auto minimum_write_span = low_latency_sink
            ? (input_is_network_ ? std::chrono::milliseconds{450} : std::chrono::milliseconds{120})
            : (input_is_network_ ? std::chrono::milliseconds{900} : std::chrono::milliseconds{250});
        const double minimum_audio_seconds = low_latency_sink
            ? (input_is_network_ ? 0.55 : 0.18)
            : (input_is_network_ ? 1.0 : 0.35);
        const bool output_consumption_observed = !input_is_network_ || elapsed_writing >= minimum_write_span;
        return elapsed_since_first_write >= minimum_output_delay
            && output_consumption_observed
            && seconds_written >= minimum_audio_seconds;
    }

    [[nodiscard]] long long outputStartDelayMsLocked() const
    {
        if (first_output_write_at_ == std::chrono::steady_clock::time_point{}) {
            return 0;
        }
        const auto elapsed = std::chrono::steady_clock::now() - first_output_write_at_;
        return std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();
    }

    [[nodiscard]] std::string sinkNameLocked() const
    {
        if (sink_kind_ == PcmOutputSinkKind::Aplay) {
            return "ALSA";
        }
        return sink_kind_ == PcmOutputSinkKind::PipeWireCat ? "PipeWire" : "ffplay";
    }

    void markFailed(std::string_view message = "Realtime PCM playback pipeline failed before output completion")
    {
        stop_requested_ = true;
        logRuntime(RuntimeLogLevel::Warn, "audio", std::string(message));
        std::lock_guard lock(state_mutex_);
        failed_ = true;
        paused_ = false;
    }

    static constexpr int kSampleRate{48000};
    static constexpr int kChannels{2};
    static constexpr std::size_t kFrameBytes{sizeof(float) * static_cast<std::size_t>(kChannels)};
    static constexpr std::size_t kVisualizationWindowSize{4096};

    audio::dsp::RealtimeDspEngine dsp_{};
    RunningPipeProcess decoder_process_{};
    RunningInputProcess sink_process_{};
    std::thread worker_{};
    std::atomic_bool stop_requested_{false};
    std::atomic_bool worker_done_{false};
    mutable std::mutex state_mutex_{};
    bool started_{false};
    bool finished_{false};
    bool failed_{false};
    bool paused_{false};
    bool input_is_network_{false};
    PcmOutputSinkKind sink_kind_{PcmOutputSinkKind::Ffplay};
    std::chrono::steady_clock::time_point first_output_write_at_{};
    std::chrono::steady_clock::time_point last_output_write_at_{};
    double position_base_seconds_{0.0};
    std::size_t frames_written_{0U};
    mutable std::mutex frame_mutex_{};
    app::AudioVisualizationFrame frame_{};
    std::vector<float> visualization_window_{};
    std::size_t clip_stats_counter_{0};
};
#endif

class ExternalAudioPlaybackBackend final : public app::AudioPlaybackBackend {
public:
    ExternalAudioPlaybackBackend()
        : executable_(resolveAudioExecutable())
        , analyzer_executable_(resolveAnalyzerExecutable())
#if defined(__linux__)
        , pcm_sink_executable_(resolvePcmOutputSink(executable_))
#endif
    {}

    ~ExternalAudioPlaybackBackend() override
    {
        stop();
    }

    [[nodiscard]] bool available() const override
    {
#if defined(__linux__)
        return (!pcm_sink_executable_.path.empty() && !analyzer_executable_.path.empty()) || !executable_.path.empty();
#else
        return !executable_.path.empty();
#endif
    }

    [[nodiscard]] std::string displayName() const override
    {
#if defined(__linux__)
        if (!pcm_sink_executable_.path.empty() && pcm_sink_executable_.kind == PcmOutputSinkKind::Aplay) {
            return "BUILT-IN ALSA";
        }
        if (!pcm_sink_executable_.path.empty() && pcm_sink_executable_.kind == PcmOutputSinkKind::PipeWireCat) {
            return "BUILT-IN PIPEWIRE";
        }
#endif
        return executable_.windows_interop ? "WINDOWS FFPLAY" : "BUILT-IN";
    }

    void setDspProfile(const audio::dsp::DspChainProfile& profile) override
    {
        {
            std::lock_guard lock(dsp_mutex_);
            dsp_profile_ = profile;
        }
#if defined(__linux__)
        if (pcm_pipeline_) {
            pcm_pipeline_->setDspProfile(profile);
        }
#endif
    }

    bool playFile(const fs::path& path, double start_seconds) override
    {
#if defined(__linux__)
        if (using_pcm_pipeline_) {
            requestPcmStopForRestart();
        } else {
            stop();
        }
#else
        stop();
#endif
        if (!available() || !fs::exists(path)) {
            logRuntime(RuntimeLogLevel::Warn, "audio", "Playback unavailable for " + pathUtf8String(path));
            return false;
        }
#if defined(__linux__)
        const auto playback_input = executable_.windows_interop ? wslPathForWindowsProcess(path) : pathUtf8String(path);
        const auto analyzer_input = analyzer_executable_.windows_interop ? wslPathForWindowsProcess(path) : pathUtf8String(path);
        return playArgument(
            playback_input,
            analyzer_input,
            start_seconds,
            "Started playback: ",
            "Failed playback: ");
#else
        const auto input = pathUtf8String(path);
        return playArgument(input, input, start_seconds, "Started playback: ", "Failed playback: ");
#endif
    }

    bool playUri(const std::string& uri, double start_seconds) override
    {
#if defined(__linux__)
        if (using_pcm_pipeline_) {
            requestPcmStopForRestart();
        } else {
            stop();
        }
#else
        stop();
#endif
        if (!available() || uri.empty()) {
            logRuntime(RuntimeLogLevel::Warn, "audio", "Remote playback unavailable");
            return false;
        }
        return playArgument(uri, uri, start_seconds, "Started stream playback: ", "Failed stream playback: ");
    }

    bool playArgument(
        const std::string& playback_input,
        const std::string& analyzer_input,
        double start_seconds,
        const std::string& ok_prefix,
        const std::string& fail_prefix)
    {
        audio::dsp::DspChainProfile active_profile{};
        {
            std::lock_guard lock(dsp_mutex_);
            active_profile = dsp_profile_;
        }
#if defined(__linux__)
        if (!pcm_sink_executable_.path.empty() && !analyzer_executable_.path.empty()) {
            auto next_pipeline = std::make_unique<RealtimePcmPlaybackPipeline>();
            const bool ok = next_pipeline->startWithoutStoppingCurrent(
                analyzer_executable_,
                pcm_sink_executable_,
                analyzer_input,
                start_seconds,
                active_profile);
            if (!ok) {
                next_pipeline->stop();
                reclaimRetiredPcmPipelines();
            } else {
                pcm_pipeline_ = std::move(next_pipeline);
                using_pcm_pipeline_ = true;
                reclaimRetiredPcmPipelines();
            }
            logRuntime(
                ok ? RuntimeLogLevel::Info : RuntimeLogLevel::Warn,
                "audio",
                std::string(ok ? ok_prefix : fail_prefix) + playback_input + " via realtime PCM DSP -> " + pcm_sink_executable_.path.string());
            return ok;
        }
#endif
        using_pcm_pipeline_ = false;
        const bool input_is_network = networkInput(playback_input);
        std::vector<std::string> args{
            "-nodisp",
            "-autoexit",
            "-loglevel",
            "warning",
            "-nostats"};
        if (input_is_network) {
            args.emplace_back("-fflags");
            args.emplace_back("nobuffer");
            args.emplace_back("-flags");
            args.emplace_back("low_delay");
            args.emplace_back("-probesize");
            args.emplace_back("32768");
            args.emplace_back("-analyzeduration");
            args.emplace_back("0");
        }

        if (start_seconds > 0.0) {
            const auto seek_ms = static_cast<int>(std::floor(start_seconds * 1000.0));
            const int whole = seek_ms / 1000;
            const int frac = std::abs(seek_ms % 1000);
            char buffer[32]{};
            std::snprintf(buffer, sizeof(buffer), "%d.%03d", whole, frac);
            args.emplace_back("-ss");
            args.emplace_back(buffer);
        }

        args.push_back(playback_input);
        const bool ok = spawnAudioProcess(process_, executable_.path, args);
        paused_ = false;
        network_playback_ = ok && input_is_network;
        analyzer_started_ = false;
        pending_analyzer_input_ = ok ? analyzer_input : std::string{};
        pending_analyzer_start_seconds_ = start_seconds;
        playback_started_confirmed_ = ok && analyzer_executable_.path.empty() && !network_playback_;
        process_started_ = ok ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point{};
        if (ok && !network_playback_) {
            analyzer_.start(analyzer_executable_, analyzer_input, start_seconds);
            analyzer_started_ = !analyzer_executable_.path.empty();
        } else {
            analyzer_.stop();
        }
        logRuntime(
            ok ? RuntimeLogLevel::Info : RuntimeLogLevel::Warn,
            "audio",
            std::string(ok ? ok_prefix : fail_prefix) + playback_input + " via " + executable_.path.string());
        return ok;
    }

    void pause() override
    {
#if defined(__linux__)
        if (using_pcm_pipeline_) {
            if (pcm_pipeline_) {
                pcm_pipeline_->pause();
            }
            return;
        }
#endif
        if (!process_.active || paused_) {
            return;
        }
        pauseAudioProcess(process_);
        analyzer_.pause();
        paused_ = true;
        logRuntime(RuntimeLogLevel::Debug, "audio", "Paused playback process");
    }

    void resume() override
    {
#if defined(__linux__)
        if (using_pcm_pipeline_) {
            if (pcm_pipeline_) {
                pcm_pipeline_->resume();
            }
            return;
        }
#endif
        if (!process_.active || !paused_) {
            return;
        }
        resumeAudioProcess(process_);
        analyzer_.resume();
        paused_ = false;
        logRuntime(RuntimeLogLevel::Debug, "audio", "Resumed playback process");
    }

    void stop() override
    {
#if defined(__linux__)
        if (using_pcm_pipeline_) {
            if (pcm_pipeline_) {
                pcm_pipeline_->stop();
                pcm_pipeline_.reset();
            }
            using_pcm_pipeline_ = false;
        }
        drainRetiredPcmPipelines();
#endif
        if (process_.active) {
            logRuntime(RuntimeLogLevel::Debug, "audio", "Stopping playback process");
        }
        stopAudioProcess(process_);
        analyzer_.stop();
        paused_ = false;
        playback_started_confirmed_ = false;
        network_playback_ = false;
        analyzer_started_ = false;
        pending_analyzer_input_.clear();
        pending_analyzer_start_seconds_ = 0.0;
        process_started_ = {};
    }

    [[nodiscard]] bool isPlaying() override
    {
#if defined(__linux__)
        if (using_pcm_pipeline_) {
            return pcm_pipeline_ && pcm_pipeline_->running();
        }
#endif
        return !paused_ && audioProcessRunning(process_);
    }

    [[nodiscard]] bool isFinished() override
    {
#if defined(__linux__)
        if (using_pcm_pipeline_) {
            return pcm_pipeline_ && pcm_pipeline_->finished();
        }
#endif
        return audioProcessFinished(process_);
    }

    [[nodiscard]] app::AudioPlaybackState state() override
    {
#if defined(__linux__)
        if (using_pcm_pipeline_) {
            return pcm_pipeline_ ? pcm_pipeline_->state() : app::AudioPlaybackState::Idle;
        }
#endif
        if (audioProcessFinished(process_)) {
            paused_ = false;
            analyzer_.stop();
            return app::AudioPlaybackState::Finished;
        }
        if (paused_ && process_.active) {
            return app::AudioPlaybackState::Paused;
        }
        if (!audioProcessRunning(process_)) {
            return app::AudioPlaybackState::Idle;
        }
        if (!playback_started_confirmed_) {
            const auto now = std::chrono::steady_clock::now();
            const auto elapsed = process_started_ == std::chrono::steady_clock::time_point{}
                ? std::chrono::steady_clock::duration::zero()
                : now - process_started_;
            if (network_playback_) {
                if (elapsed < kNetworkPlaybackUiStartDelay) {
                    return app::AudioPlaybackState::Starting;
                }
                startAnalyzerIfNeeded();
                if (analyzer_executable_.path.empty()
                    || analyzer_.currentFrame().available
                    || elapsed >= kNetworkPlaybackUiStartDelay + kNetworkAnalyzerWarmupLimit) {
                    playback_started_confirmed_ = true;
                } else {
                    return app::AudioPlaybackState::Starting;
                }
            } else if (!analyzer_executable_.path.empty()) {
                if (analyzer_.currentFrame().available) {
                    playback_started_confirmed_ = true;
                } else if (process_started_ != std::chrono::steady_clock::time_point{}
                    && elapsed < std::chrono::seconds{45}) {
                    return app::AudioPlaybackState::Starting;
                }
            }
        }
        return app::AudioPlaybackState::Playing;
    }

    [[nodiscard]] std::optional<double> positionSeconds() const override
    {
#if defined(__linux__)
        if (using_pcm_pipeline_) {
            return pcm_pipeline_ ? pcm_pipeline_->positionSeconds() : std::nullopt;
        }
#endif
        return std::nullopt;
    }

    [[nodiscard]] app::AudioVisualizationFrame visualizationFrame() const override
    {
#if defined(__linux__)
        if (using_pcm_pipeline_) {
            return pcm_pipeline_ ? pcm_pipeline_->visualizationFrame() : app::AudioVisualizationFrame{};
        }
#endif
        return analyzer_.currentFrame();
    }

private:
#if defined(__linux__)
    void requestPcmStopForRestart()
    {
        if (!using_pcm_pipeline_ || !pcm_pipeline_) {
            return;
        }
        pcm_pipeline_->requestStop();
        retired_pcm_pipelines_.push_back(std::move(pcm_pipeline_));
        using_pcm_pipeline_ = false;
    }

    void reclaimRetiredPcmPipelines()
    {
        if (retired_pcm_pipelines_.empty()) {
            return;
        }
        std::vector<std::unique_ptr<RealtimePcmPlaybackPipeline>> still_retiring{};
        still_retiring.reserve(retired_pcm_pipelines_.size());
        for (auto& pipeline : retired_pcm_pipelines_) {
            if (!pipeline) {
                continue;
            }
            if (pipeline->stopped()) {
                pipeline->waitForStop();
            } else {
                still_retiring.push_back(std::move(pipeline));
            }
        }
        retired_pcm_pipelines_ = std::move(still_retiring);
    }

    void drainRetiredPcmPipelines()
    {
        for (auto& pipeline : retired_pcm_pipelines_) {
            if (pipeline) {
                pipeline->requestStop();
                pipeline->waitForStop();
            }
        }
        retired_pcm_pipelines_.clear();
    }
#endif

    void startAnalyzerIfNeeded()
    {
        if (analyzer_started_ || analyzer_executable_.path.empty() || pending_analyzer_input_.empty()) {
            return;
        }
        analyzer_.start(analyzer_executable_, pending_analyzer_input_, pending_analyzer_start_seconds_);
        analyzer_started_ = true;
    }

    AudioExecutable executable_{};
    AudioExecutable analyzer_executable_{};
#if defined(__linux__)
    PcmOutputSink pcm_sink_executable_{};
    std::unique_ptr<RealtimePcmPlaybackPipeline> pcm_pipeline_{};
    std::vector<std::unique_ptr<RealtimePcmPlaybackPipeline>> retired_pcm_pipelines_{};
#endif
    RunningProcess process_{};
    RealtimePcmSpectrumAnalyzer analyzer_{};
    mutable std::mutex dsp_mutex_{};
    audio::dsp::DspChainProfile dsp_profile_{};
    std::chrono::steady_clock::time_point process_started_{};
    std::string pending_analyzer_input_{};
    double pending_analyzer_start_seconds_{0.0};
    bool playback_started_confirmed_{false};
    bool network_playback_{false};
    bool analyzer_started_{false};
    bool paused_{false};
    bool using_pcm_pipeline_{false};
};

} // namespace

std::shared_ptr<app::AudioPlaybackBackend> createHostAudioPlaybackBackend()
{
    return std::make_shared<ExternalAudioPlaybackBackend>();
}

} // namespace lofibox::platform::host
