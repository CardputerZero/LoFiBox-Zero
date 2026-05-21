// SPDX-License-Identifier: GPL-3.0-or-later

#include "audio/audio_pipeline_controller.h"

namespace lofibox::audio {

void AudioPipelineController::bind(app::RuntimeServices* services) noexcept
{
    services_ = services;
}

bool AudioPipelineController::startFile(const std::filesystem::path& path, double start_seconds)
{
    if (auto* audio_backend = backend()) {
        audio_backend->setDspProfile(dsp_chain_.profile());
        return audio_backend->playFile(path, start_seconds);
    }
    return false;
}

bool AudioPipelineController::startUri(const std::string& uri, double start_seconds)
{
    if (auto* audio_backend = backend()) {
        audio_backend->setDspProfile(dsp_chain_.profile());
        return audio_backend->playUri(uri, start_seconds);
    }
    return false;
}

void AudioPipelineController::pause() noexcept
{
    if (auto* audio_backend = backend()) {
        audio_backend->pause();
    }
}

void AudioPipelineController::resume() noexcept
{
    if (auto* audio_backend = backend()) {
        audio_backend->resume();
    }
}

void AudioPipelineController::stop() noexcept
{
    if (auto* audio_backend = backend()) {
        audio_backend->stop();
    }
}

app::AudioPlaybackState AudioPipelineController::state() const
{
    if (const auto* audio_backend = backend()) {
        return const_cast<app::AudioPlaybackBackend*>(audio_backend)->state();
    }
    return app::AudioPlaybackState::Failed;
}

std::optional<double> AudioPipelineController::positionSeconds() const
{
    if (const auto* audio_backend = backend()) {
        return audio_backend->positionSeconds();
    }
    return std::nullopt;
}

app::AudioVisualizationFrame AudioPipelineController::visualizationFrame() const
{
    if (const auto* audio_backend = backend()) {
        return audio_backend->visualizationFrame();
    }
    return {};
}

void AudioPipelineController::setDspProfile(dsp::DspChainProfile profile)
{
    dsp_chain_.setProfile(profile);
    if (auto* audio_backend = backend()) {
        audio_backend->setDspProfile(dsp_chain_.profile());
    }
}

const dsp::DspChainProfile& AudioPipelineController::dspProfile() const noexcept
{
    return dsp_chain_.profile();
}

float AudioPipelineController::processDspSample(float sample) const noexcept
{
    return dsp_chain_.processSample(sample);
}

app::AudioPlaybackBackend* AudioPipelineController::backend() const noexcept
{
    if (!services_ || !services_->playback.audio_backend) {
        return nullptr;
    }
    return services_->playback.audio_backend.get();
}

} // namespace lofibox::audio
