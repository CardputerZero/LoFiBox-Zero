// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <array>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace lofibox::audio::dsp {

enum class EqProfileType {
    System,
    User,
    DeviceSpecific,
    ContentSpecific,
};

enum class ReplayGainMode {
    Off,
    Track,
    Album,
};

enum class FilterKind {
    Bell,
    LowPass,
    HighPass,
};

struct EqBand {
    double frequency_hz{1000.0};
    double gain_db{0.0};
    double q{1.0};
    bool enabled{true};
    FilterKind kind{FilterKind::Bell};
};

struct ParametricEqBand {
    double center_frequency_hz{1000.0};
    double gain_db{0.0};
    double q{1.0};
    bool enabled{true};
};

struct OutputDeviceBinding {
    std::string output_device_id{};
    std::string eq_profile_id{};
};

struct ContentTypeBinding {
    std::string content_type{};
    std::string eq_profile_id{};
};

struct EqProfile {
    std::string id{"flat"};
    std::string name{"Flat"};
    EqProfileType type{EqProfileType::System};
    bool enabled{false};
    bool bypass{false};
    double preamp_db{0.0};
    std::array<EqBand, 10> bands{};
    std::vector<ParametricEqBand> parametric_bands{};
    std::optional<double> high_pass_hz{};
    std::optional<double> low_pass_hz{};
    double balance{-0.0};
    bool loudness_enabled{false};
    double loudness_strength{0.0};
    bool limiter_enabled{false};
    double limiter_ceiling_db{-1.0};
    ReplayGainMode replaygain_mode{ReplayGainMode::Off};
    bool is_default{false};
};

struct ReplayGainProfile {
    bool enabled{false};
    double gain_db{0.0};
    ReplayGainMode mode{ReplayGainMode::Off};
    double target_lufs{-18.0};
};

struct LimiterProfile {
    bool enabled{false};
    double ceiling_db{-1.0};
};

struct AudioEffectProfile {
    std::string plugin_id{};
    std::string effect_id{};
    std::string name{};
    double intensity{1.0};
};

struct DspChainProfile {
    std::string active_profile_id{"flat"};
    EqProfile eq{};
    ReplayGainProfile replay_gain{};
    LimiterProfile limiter{};
    AudioEffectProfile effect{};
    double volume_db{0.0};
};

class DspChain {
public:
    void setProfile(DspChainProfile profile);
    [[nodiscard]] const DspChainProfile& profile() const noexcept;
    [[nodiscard]] float processSample(float sample) const noexcept;

private:
    DspChainProfile profile_{};
};

struct EqSessionState {
    std::string current_profile_id{"flat"};
    bool enabled{false};
    bool bypass{false};
    double last_effective_gain_db{0.0};
};

class PresetRepository {
public:
    PresetRepository();

    [[nodiscard]] std::vector<EqProfile> profiles() const;
    [[nodiscard]] std::optional<EqProfile> profile(std::string_view id) const;
    bool save(EqProfile profile);
    bool remove(std::string_view id);
    [[nodiscard]] EqProfile duplicate(std::string_view source_id, std::string id, std::string name);
    bool rename(std::string_view id, std::string name);
    [[nodiscard]] std::string exportProfile(std::string_view id) const;
    bool importProfile(std::string_view serialized);

private:
    std::unordered_map<std::string, EqProfile> profiles_{};
};

class EqManager {
public:
    EqManager();

    [[nodiscard]] const EqSessionState& session() const noexcept;
    [[nodiscard]] const PresetRepository& presets() const noexcept;
    [[nodiscard]] PresetRepository& presets() noexcept;
    [[nodiscard]] std::optional<EqProfile> currentProfile() const;
    [[nodiscard]] DspChainProfile chainProfile() const;

    bool selectProfile(std::string_view id);
    void setEnabled(bool enabled) noexcept;
    void setBypass(bool bypass) noexcept;
    bool updateGraphicBand(std::size_t band_index, double gain_db);
    bool updatePreamp(double preamp_db);
    bool setBalance(double balance);
    bool setLoudness(bool enabled, double strength);
    bool setLimiter(bool enabled, double ceiling_db);
    bool setReplayGain(ReplayGainMode mode, double gain_db, double target_lufs);
    bool setHighPass(std::optional<double> cutoff_hz);
    bool setLowPass(std::optional<double> cutoff_hz);
    bool setParametricBands(std::vector<ParametricEqBand> bands);
    bool bindOutputDevice(std::string output_device_id, std::string profile_id);
    bool bindContentType(std::string content_type, std::string profile_id);
    [[nodiscard]] std::optional<std::string> profileForOutputDevice(std::string_view output_device_id) const;
    [[nodiscard]] std::optional<std::string> profileForContentType(std::string_view content_type) const;

private:
    bool saveCurrent(EqProfile profile);

    PresetRepository presets_{};
    EqSessionState session_{};
    std::vector<OutputDeviceBinding> device_bindings_{};
    std::vector<ContentTypeBinding> content_bindings_{};
};

[[nodiscard]] EqProfile tenBandFlatEqProfile();
[[nodiscard]] EqProfile builtinEqPreset(std::string_view id);
[[nodiscard]] std::vector<EqProfile> builtinEqPresets();
[[nodiscard]] double smoothedGainDb(double current_db, double target_db, double smoothing);

} // namespace lofibox::audio::dsp
