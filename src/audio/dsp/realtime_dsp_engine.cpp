// SPDX-License-Identifier: GPL-3.0-or-later

#include "audio/dsp/realtime_dsp_engine.h"

#include "audio/dsp/audio_effect_registry.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>
#include <string_view>

namespace lofibox::audio::dsp {
namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kTwoPi = kPi * 2.0;
constexpr double kGainSmoothing = 0.28;

enum class RemixProcessor {
    Off,
    Radio,
    Tape,
    Vinyl,
};

struct RemixCoefficients {
    RealtimeDspEngine::BiquadCoefficients high_pass{};
    RealtimeDspEngine::BiquadCoefficients low_pass{};
    RealtimeDspEngine::BiquadCoefficients tone_a{};
    RealtimeDspEngine::BiquadCoefficients tone_b{};
    double drive{1.0};
    double wet{1.0};
    double output_gain{1.0};
};

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

[[nodiscard]] RemixProcessor remixProcessor(std::string_view effect_id) noexcept
{
    if (effect_id == remixRadioEffectId()) {
        return RemixProcessor::Radio;
    }
    if (effect_id == remixTapeEffectId()) {
        return RemixProcessor::Tape;
    }
    if (effect_id == remixVinylEffectId()) {
        return RemixProcessor::Vinyl;
    }
    return RemixProcessor::Off;
}

[[nodiscard]] RemixCoefficients remixCoefficients(RemixProcessor processor, double sample_rate_hz) noexcept
{
    switch (processor) {
    case RemixProcessor::Radio:
        return {
            makeHighPass(sample_rate_hz, 285.0),
            makeLowPass(sample_rate_hz, 3600.0),
            makePeakingEq(sample_rate_hz, 1150.0, 4.6, 0.8),
            makePeakingEq(sample_rate_hz, 2450.0, 2.0, 1.1),
            1.75,
            1.0,
            0.90,
        };
    case RemixProcessor::Tape:
        return {
            makeHighPass(sample_rate_hz, 38.0),
            makeLowPass(sample_rate_hz, 9800.0),
            makePeakingEq(sample_rate_hz, 180.0, 2.4, 0.7),
            makePeakingEq(sample_rate_hz, 4300.0, -2.0, 0.9),
            1.42,
            0.96,
            0.98,
        };
    case RemixProcessor::Vinyl:
        return {
            makeHighPass(sample_rate_hz, 46.0),
            makeLowPass(sample_rate_hz, 12800.0),
            makePeakingEq(sample_rate_hz, 120.0, 1.4, 0.8),
            makePeakingEq(sample_rate_hz, 5200.0, 0.9, 1.2),
            1.22,
            0.92,
            0.98,
        };
    case RemixProcessor::Off:
        break;
    }
    return {};
}

[[nodiscard]] double softSaturate(double sample, double drive) noexcept
{
    const double clamped_drive = std::clamp(drive, 1.0, 4.0);
    const double normalizer = std::tanh(clamped_drive);
    if (normalizer <= 0.0) {
        return sample;
    }
    return std::tanh(sample * clamped_drive) / normalizer;
}

void advancePhase(double& phase, double frequency_hz, double sample_rate_hz) noexcept
{
    phase += kTwoPi * frequency_hz / sample_rate_hz;
    if (phase >= kTwoPi) {
        phase = std::fmod(phase, kTwoPi);
    }
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
    resetRemixState();
}

void RealtimeDspEngine::setProfile(DspChainProfile profile)
{
    std::lock_guard lock(mutex_);
    const bool parametric_count_changed = profile.eq.parametric_bands.size() != profile_.eq.parametric_bands.size();
    const bool effect_changed = profile.effect.plugin_id != profile_.effect.plugin_id
        || profile.effect.effect_id != profile_.effect.effect_id;
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
    if (effect_changed) {
        resetRemixState();
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

void RealtimeDspEngine::resetRemixState() noexcept
{
    remix_wow_phase_ = 0.0;
    remix_flutter_phase_ = 0.0;
    remix_radio_phase_ = 0.0;
    remix_noise_state_ = 0x4d595df4U;
    vinyl_dust_ = 0.0;
    vinyl_scratch_ = 0.0;
    for (auto& channel : channels_) {
        channel.remix_high_pass = {};
        channel.remix_low_pass = {};
        channel.remix_tone_a = {};
        channel.remix_tone_b = {};
    }
}

double RealtimeDspEngine::randomSigned() noexcept
{
    std::uint32_t x = remix_noise_state_;
    x ^= x << 13U;
    x ^= x >> 17U;
    x ^= x << 5U;
    remix_noise_state_ = x;
    const double unit = static_cast<double>(x) / static_cast<double>(std::numeric_limits<std::uint32_t>::max());
    return (unit * 2.0) - 1.0;
}

RealtimeDspEngine::RemixFrameState RealtimeDspEngine::nextRemixFrame(std::string_view effect_id, double sample_rate_hz) noexcept
{
    RemixFrameState frame{};
    const auto processor = remixProcessor(effect_id);
    if (processor == RemixProcessor::Off || sample_rate_hz <= 0.0) {
        return frame;
    }

    advancePhase(remix_wow_phase_, 0.36, sample_rate_hz);
    advancePhase(remix_flutter_phase_, 6.4, sample_rate_hz);
    advancePhase(remix_radio_phase_, 5.7, sample_rate_hz);

    switch (processor) {
    case RemixProcessor::Radio:
        frame.modulation = 0.955
            + (std::sin(remix_radio_phase_) * 0.035)
            + (randomSigned() * 0.006);
        frame.noise = randomSigned() * 0.0038;
        break;
    case RemixProcessor::Tape:
        frame.modulation = 1.0
            + (std::sin(remix_wow_phase_) * 0.011)
            + (std::sin(remix_flutter_phase_) * 0.004);
        frame.noise = randomSigned() * 0.0014;
        break;
    case RemixProcessor::Vinyl:
        frame.modulation = 1.0 + (std::sin(remix_wow_phase_) * 0.0025);
        frame.noise = randomSigned() * 0.0022;
        if (randomSigned() > 0.9997) {
            vinyl_dust_ += randomSigned() * 0.09;
        }
        if (randomSigned() > 0.99996) {
            vinyl_scratch_ += randomSigned() * 0.20;
        }
        frame.crackle = vinyl_dust_ + vinyl_scratch_;
        vinyl_dust_ *= 0.88;
        vinyl_scratch_ *= 0.985;
        break;
    case RemixProcessor::Off:
        break;
    }
    return frame;
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
    const auto remix_processor = remixProcessor(profile_.effect.effect_id);
    const bool remix_active = remix_processor != RemixProcessor::Off;
    const auto remix_coefficients = remixCoefficients(remix_processor, sample_rate_hz);
    const double remix_intensity = remix_active ? std::clamp(profile_.effect.intensity, 0.0, 1.25) : 0.0;
    const double remix_wet = std::clamp(remix_coefficients.wet * remix_intensity, 0.0, 1.0);
    const double remix_drive = 1.0 + ((remix_coefficients.drive - 1.0) * remix_intensity);

    for (std::size_t frame = 0; frame < frame_count; ++frame) {
        double dry_mono = 0.0;
        if (remix_active) {
            for (int channel_index = 0; channel_index < channels; ++channel_index) {
                dry_mono += samples[(frame * static_cast<std::size_t>(channels)) + static_cast<std::size_t>(channel_index)];
            }
            dry_mono /= static_cast<double>(channels);
        }
        const auto remix_frame = remix_active ? nextRemixFrame(profile_.effect.effect_id, sample_rate_hz) : RemixFrameState{};
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
            if (remix_active && remix_wet > 0.0) {
                const double dry = value;
                if (remix_processor == RemixProcessor::Radio && channels >= 2) {
                    value = (value * 0.34) + (dry_mono * 0.66);
                }
                value = channel.remix_high_pass.process(value, remix_coefficients.high_pass);
                value = channel.remix_low_pass.process(value, remix_coefficients.low_pass);
                value = channel.remix_tone_a.process(value, remix_coefficients.tone_a);
                value = channel.remix_tone_b.process(value, remix_coefficients.tone_b);
                value *= remix_frame.modulation;
                const double stereo_noise_bias = channels >= 2
                    ? (channel_index == 0 ? 0.94 : (channel_index == 1 ? 1.06 : 1.0))
                    : 1.0;
                value += (remix_frame.noise + remix_frame.crackle) * stereo_noise_bias * remix_intensity;
                value = softSaturate(value, remix_drive) * remix_coefficients.output_gain;
                value = (dry * (1.0 - remix_wet)) + (value * remix_wet);
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

ClipStats RealtimeDspEngine::clipStats() const
{
    std::lock_guard lock(mutex_);
    return clip_stats_;
}

void RealtimeDspEngine::resetClipStats()
{
    std::lock_guard lock(mutex_);
    clip_stats_ = {};
}

} // namespace lofibox::audio::dsp
