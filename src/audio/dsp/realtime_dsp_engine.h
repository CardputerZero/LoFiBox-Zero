// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstddef>
#include <cstdint>
#include <mutex>
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
    };

    void reset(double sample_rate_hz, int channels);
    void setProfile(DspChainProfile profile);
    [[nodiscard]] DspChainProfile profile() const;
    void processInterleaved(float* samples, std::size_t frame_count, int channels, double sample_rate_hz);
    [[nodiscard]] ClipStats clipStats();

private:
    void ensureState(int channels, double sample_rate_hz);

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

    ClipStats clip_stats_{};
};

} // namespace lofibox::audio::dsp
