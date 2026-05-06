// SPDX-License-Identifier: GPL-3.0-or-later

#include "audio/dsp/realtime_dsp_engine.h"

#include <algorithm>
#include <cmath>
#include <optional>

namespace lofibox::audio::dsp {
namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kGainSmoothing = 0.28;

[[nodiscard]] double dbToLinear(double db) noexcept
{
    return std::pow(10.0, db / 20.0);
}

[[nodiscard]] double smooth(double current, double target) noexcept
{
    return smoothedGainDb(current, target, kGainSmoothing);
}

[[nodiscard]] double safeFrequency(double frequency_hz, double sample_rate_hz) noexcept
{
    const double nyquist = std::max(1.0, sample_rate_hz * 0.5);
    return std::clamp(frequency_hz, 10.0, nyquist - 10.0);
}

[[nodiscard]] RealtimeDspEngine::BiquadCoefficients makePeakingEq(
    double sample_rate_hz,
    double frequency_hz,
    double gain_db,
    double q) noexcept
{
    const double clamped_q = std::clamp(q, 0.1, 24.0);
    const double frequency = safeFrequency(frequency_hz, sample_rate_hz);
    const double a = std::pow(10.0, gain_db / 40.0);
    const double w0 = 2.0 * kPi * frequency / sample_rate_hz;
    const double alpha = std::sin(w0) / (2.0 * clamped_q);
    const double cos_w0 = std::cos(w0);

    const double b0 = 1.0 + (alpha * a);
    const double b1 = -2.0 * cos_w0;
    const double b2 = 1.0 - (alpha * a);
    const double a0 = 1.0 + (alpha / a);
    const double a1 = -2.0 * cos_w0;
    const double a2 = 1.0 - (alpha / a);

    return {
        b0 / a0,
        b1 / a0,
        b2 / a0,
        a1 / a0,
        a2 / a0,
    };
}

[[nodiscard]] RealtimeDspEngine::BiquadCoefficients makeHighPass(double sample_rate_hz, double frequency_hz) noexcept
{
    const double frequency = safeFrequency(frequency_hz, sample_rate_hz);
    const double w0 = 2.0 * kPi * frequency / sample_rate_hz;
    const double cos_w0 = std::cos(w0);
    const double sin_w0 = std::sin(w0);
    const double alpha = sin_w0 / std::sqrt(2.0);
    const double b0 = (1.0 + cos_w0) * 0.5;
    const double b1 = -(1.0 + cos_w0);
    const double b2 = (1.0 + cos_w0) * 0.5;
    const double a0 = 1.0 + alpha;
    const double a1 = -2.0 * cos_w0;
    const double a2 = 1.0 - alpha;
    return {b0 / a0, b1 / a0, b2 / a0, a1 / a0, a2 / a0};
}

[[nodiscard]] RealtimeDspEngine::BiquadCoefficients makeLowPass(double sample_rate_hz, double frequency_hz) noexcept
{
    const double frequency = safeFrequency(frequency_hz, sample_rate_hz);
    const double w0 = 2.0 * kPi * frequency / sample_rate_hz;
    const double cos_w0 = std::cos(w0);
    const double sin_w0 = std::sin(w0);
    const double alpha = sin_w0 / std::sqrt(2.0);
    const double b0 = (1.0 - cos_w0) * 0.5;
    const double b1 = 1.0 - cos_w0;
    const double b2 = (1.0 - cos_w0) * 0.5;
    const double a0 = 1.0 + alpha;
    const double a1 = -2.0 * cos_w0;
    const double a2 = 1.0 - alpha;
    return {b0 / a0, b1 / a0, b2 / a0, a1 / a0, a2 / a0};
}

[[nodiscard]] double targetGraphicGain(const EqProfile& profile, std::size_t index) noexcept
{
    if (!profile.enabled || profile.bypass || index >= profile.bands.size() || !profile.bands[index].enabled) {
        return 0.0;
    }
    return std::clamp(profile.bands[index].gain_db, -24.0, 24.0);
}

[[nodiscard]] double targetParametricGain(const EqProfile& profile, std::size_t index) noexcept
{
    if (!profile.enabled || profile.bypass || index >= profile.parametric_bands.size() || !profile.parametric_bands[index].enabled) {
        return 0.0;
    }
    return std::clamp(profile.parametric_bands[index].gain_db, -24.0, 24.0);
}

} // namespace

double RealtimeDspEngine::BiquadState::process(double sample, const BiquadCoefficients& coefficients) noexcept
{
    const double output = (coefficients.b0 * sample)
        + (coefficients.b1 * x1)
        + (coefficients.b2 * x2)
        - (coefficients.a1 * y1)
        - (coefficients.a2 * y2);
    x2 = x1;
    x1 = sample;
    y2 = y1;
    y1 = output;
    return output;
}

void RealtimeDspEngine::reset(double sample_rate_hz, int channels)
{
    std::lock_guard lock(mutex_);
    sample_rate_hz_ = sample_rate_hz;
    channels_.assign(static_cast<std::size_t>(std::max(0, channels)), {});
    smoothed_graphic_gains_.assign(profile_.eq.bands.size(), 0.0);
    smoothed_parametric_gains_.assign(profile_.eq.parametric_bands.size(), 0.0);
}

void RealtimeDspEngine::setProfile(DspChainProfile profile)
{
    std::lock_guard lock(mutex_);
    const bool parametric_count_changed = profile.eq.parametric_bands.size() != profile_.eq.parametric_bands.size();
    profile_ = std::move(profile);
    if (smoothed_graphic_gains_.size() != profile_.eq.bands.size()) {
        smoothed_graphic_gains_.assign(profile_.eq.bands.size(), 0.0);
    }
    if (parametric_count_changed || smoothed_parametric_gains_.size() != profile_.eq.parametric_bands.size()) {
        smoothed_parametric_gains_.assign(profile_.eq.parametric_bands.size(), 0.0);
        for (auto& channel : channels_) {
            channel.parametric_bands.clear();
        }
    }
}

DspChainProfile RealtimeDspEngine::profile() const
{
    std::lock_guard lock(mutex_);
    return profile_;
}

void RealtimeDspEngine::ensureState(int channels, double sample_rate_hz)
{
    if (channels <= 0) {
        channels_.clear();
        return;
    }
    const bool shape_changed = static_cast<int>(channels_.size()) != channels
        || std::fabs(sample_rate_hz_ - sample_rate_hz) > 1.0;
    if (shape_changed) {
        sample_rate_hz_ = sample_rate_hz;
        channels_.assign(static_cast<std::size_t>(channels), {});
    }
    for (auto& channel : channels_) {
        if (channel.graphic_bands.size() != profile_.eq.bands.size()) {
            channel.graphic_bands.assign(profile_.eq.bands.size(), {});
        }
        if (channel.parametric_bands.size() != profile_.eq.parametric_bands.size()) {
            channel.parametric_bands.assign(profile_.eq.parametric_bands.size(), {});
        }
    }
}

void RealtimeDspEngine::processInterleaved(float* samples, std::size_t frame_count, int channels, double sample_rate_hz)
{
    if (samples == nullptr || frame_count == 0U || channels <= 0 || sample_rate_hz <= 0.0) {
        return;
    }

    std::lock_guard lock(mutex_);
    ensureState(channels, sample_rate_hz);
    if (channels_.empty()) {
        return;
    }

    std::vector<BiquadCoefficients> graphic_coefficients{};
    graphic_coefficients.reserve(profile_.eq.bands.size());
    for (std::size_t index = 0; index < profile_.eq.bands.size(); ++index) {
        smoothed_graphic_gains_[index] = smooth(smoothed_graphic_gains_[index], targetGraphicGain(profile_.eq, index));
        const auto& band = profile_.eq.bands[index];
        graphic_coefficients.push_back(makePeakingEq(sample_rate_hz, band.frequency_hz, smoothed_graphic_gains_[index], band.q));
    }

    std::vector<BiquadCoefficients> parametric_coefficients{};
    parametric_coefficients.reserve(profile_.eq.parametric_bands.size());
    for (std::size_t index = 0; index < profile_.eq.parametric_bands.size(); ++index) {
        smoothed_parametric_gains_[index] = smooth(smoothed_parametric_gains_[index], targetParametricGain(profile_.eq, index));
        const auto& band = profile_.eq.parametric_bands[index];
        parametric_coefficients.push_back(makePeakingEq(sample_rate_hz, band.center_frequency_hz, smoothed_parametric_gains_[index], band.q));
    }

    const bool eq_active = profile_.eq.enabled && !profile_.eq.bypass;
    smoothed_preamp_db_ = smooth(smoothed_preamp_db_, eq_active ? profile_.eq.preamp_db : 0.0);
    smoothed_loudness_db_ = smooth(
        smoothed_loudness_db_,
        eq_active && profile_.eq.loudness_enabled ? std::clamp(profile_.eq.loudness_strength, 0.0, 1.0) * 3.0 : 0.0);
    smoothed_replay_gain_db_ = smooth(smoothed_replay_gain_db_, profile_.replay_gain.enabled ? profile_.replay_gain.gain_db : 0.0);
    smoothed_volume_db_ = smooth(smoothed_volume_db_, profile_.volume_db);
    const double scalar_gain = dbToLinear(smoothed_preamp_db_ + smoothed_loudness_db_ + smoothed_replay_gain_db_ + smoothed_volume_db_);
    const double limiter_ceiling = dbToLinear(
        profile_.limiter.enabled ? profile_.limiter.ceiling_db : profile_.eq.limiter_ceiling_db);
    const auto high_pass = profile_.eq.high_pass_hz ? std::optional<BiquadCoefficients>{makeHighPass(sample_rate_hz, *profile_.eq.high_pass_hz)} : std::nullopt;
    const auto low_pass = profile_.eq.low_pass_hz ? std::optional<BiquadCoefficients>{makeLowPass(sample_rate_hz, *profile_.eq.low_pass_hz)} : std::nullopt;

    for (std::size_t frame = 0; frame < frame_count; ++frame) {
        for (int channel_index = 0; channel_index < channels; ++channel_index) {
            auto& channel = channels_[static_cast<std::size_t>(channel_index)];
            double value = samples[(frame * static_cast<std::size_t>(channels)) + static_cast<std::size_t>(channel_index)];
            if (eq_active) {
                for (std::size_t index = 0; index < channel.graphic_bands.size(); ++index) {
                    value = channel.graphic_bands[index].process(value, graphic_coefficients[index]);
                }
                for (std::size_t index = 0; index < channel.parametric_bands.size(); ++index) {
                    value = channel.parametric_bands[index].process(value, parametric_coefficients[index]);
                }
                if (high_pass) {
                    value = channel.high_pass.process(value, *high_pass);
                }
                if (low_pass) {
                    value = channel.low_pass.process(value, *low_pass);
                }
            }
            value *= scalar_gain;
            if (channels >= 2 && profile_.eq.enabled && !profile_.eq.bypass && std::fabs(profile_.eq.balance) > 0.001) {
                const double balance = std::clamp(profile_.eq.balance, -1.0, 1.0);
                if (channel_index == 0 && balance > 0.0) {
                    value *= 1.0 - balance;
                } else if (channel_index == 1 && balance < 0.0) {
                    value *= 1.0 + balance;
                }
            }
            const double abs_value = std::fabs(value);
            if (abs_value > static_cast<double>(clip_stats_.peak_before)) {
                clip_stats_.peak_before = static_cast<float>(abs_value);
            }
            if (abs_value > limiter_ceiling) {
                ++clip_stats_.over_ceiling_count;
            }
            if (abs_value > 1.0) {
                ++clip_stats_.over_fullscale_count;
            }
            const double clamped = std::clamp(value, -1.0, 1.0);
            samples[(frame * static_cast<std::size_t>(channels)) + static_cast<std::size_t>(channel_index)] =
                static_cast<float>(clamped);
            const double abs_clamped = std::fabs(clamped);
            if (abs_clamped > static_cast<double>(clip_stats_.peak_after)) {
                clip_stats_.peak_after = static_cast<float>(abs_clamped);
            }
        }
    }
}

ClipStats RealtimeDspEngine::clipStats()
{
    std::lock_guard lock(mutex_);
    ClipStats result = clip_stats_;
    clip_stats_ = {};
    return result;
}

} // namespace lofibox::audio::dsp
