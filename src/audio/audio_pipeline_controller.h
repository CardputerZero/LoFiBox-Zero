// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <filesystem>
#include <optional>
#include <string>

#include "app/audio_visualization.h"
#include "app/runtime_services.h"
#include "audio/dsp/dsp_chain.h"

namespace lofibox::audio {

class AudioPipelineController {
public:
    void bind(app::RuntimeServices* services) noexcept;

    [[nodiscard]] bool startFile(const std::filesystem::path& path, double start_seconds);
    [[nodiscard]] bool startUri(const std::string& uri, double start_seconds);
    void pause() noexcept;
    void resume() noexcept;
    void stop() noexcept;

    [[nodiscard]] app::AudioPlaybackState state() const;
    [[nodiscard]] std::optional<double> positionSeconds() const;
    [[nodiscard]] app::AudioVisualizationFrame visualizationFrame() const;
    void setDspProfile(dsp::DspChainProfile profile);
    [[nodiscard]] const dsp::DspChainProfile& dspProfile() const noexcept;
    [[nodiscard]] float processDspSample(float sample) const noexcept;

private:
    [[nodiscard]] app::AudioPlaybackBackend* backend() const noexcept;

    app::RuntimeServices* services_{nullptr};
    dsp::DspChain dsp_chain_{};
};

} // namespace lofibox::audio
