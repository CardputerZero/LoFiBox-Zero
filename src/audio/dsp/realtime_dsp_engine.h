// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string_view>
#include <vector>

#include "audio/dsp/dsp_chain.h"

namespace lofibox::audio::dsp {

struct ClipStats {
    std::uint64_t over_ceiling_count{0};
    std::uint64_t over_fullscale_count{0};
    float peak_before{0.0f};
    float peak_after{0.0f};
};

class RealtimeDspEngine {
public:
    struct BiquadCoefficients {
        double b0{1.0};
        double b1{0.0};
        double b2{0.0};
        double a1{0.0};
        double a2{0.0};
    };

    struct BiquadState {
        double x1{0.0};
        double x2{0.0};
        double y1{0.0};
        double y2{0.0};

        [[nodiscard]] double process(double sample, const BiquadCoefficients& coefficients) noexcept;
    };

    struct ChannelState {
        std::vector<BiquadState> graphic_bands{};
        std::vector<BiquadState> parametric_bands{};
        BiquadState high_pass{};
        BiquadState low_pass{};
        BiquadState remix_high_pass{};
        BiquadState remix_low_pass{};
        BiquadState remix_tone_a{};
        BiquadState remix_tone_b{};
    };

    struct RemixFrameState {
        double modulation{1.0};
        double noise{0.0};
        double crackle{0.0};
    };

    void reset(double sample_rate_hz, int channels);
    void setProfile(DspChainProfile profile);
    [[nodiscard]] DspChainProfile profile() const;
    void processInterleaved(float* samples, std::size_t frame_count, int channels, double sample_rate_hz);
    [[nodiscard]] ClipStats clipStats() const;
    void resetClipStats();

private:
    void ensureState(int channels, double sample_rate_hz);
    void resetRemixState() noexcept;
    [[nodiscard]] double randomSigned() noexcept;
    [[nodiscard]] RemixFrameState nextRemixFrame(std::string_view effect_id, double sample_rate_hz) noexcept;

    mutable std::mutex mutex_{};
    DspChainProfile profile_{};
    std::vector<ChannelState> channels_{};
    double sample_rate_hz_{0.0};
    std::vector<double> smoothed_graphic_gains_{};
    std::vector<double> smoothed_parametric_gains_{};
    double smoothed_preamp_db_{0.0};
    double smoothed_loudness_db_{0.0};
    double smoothed_replay_gain_db_{0.0};
    double smoothed_volume_db_{0.0};

    double remix_wow_phase_{0.0};
    double remix_flutter_phase_{0.0};
    double remix_radio_phase_{0.0};
    std::uint32_t remix_noise_state_{0x4d595df4U};
    double vinyl_dust_{0.0};
    double vinyl_scratch_{0.0};

    ClipStats clip_stats_{};
};

} // namespace lofibox::audio::dsp
