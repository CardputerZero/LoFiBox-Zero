// SPDX-License-Identifier: GPL-3.0-or-later

#include "tui/tui_command_palette.h"

#include <algorithm>
#include <cctype>
#include <string>
#include <utility>

namespace lofibox::tui {
namespace {

std::string normalize(std::string_view value)
{
    std::string result{value};
    std::transform(result.begin(), result.end(), result.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return result;
}

runtime::RuntimeCommand makeCommand(runtime::RuntimeCommandKind kind, runtime::RuntimeCommandPayload payload = {})
{
    return runtime::RuntimeCommand{kind, std::move(payload), runtime::CommandOrigin::Automation, "tui:palette"};
}

} // namespace

bool commandPaletteRejectsShell(std::string_view text) noexcept
{
    return !text.empty() && (text.front() == '!' || text == "shell" || text == "exec");
}

std::optional<runtime::RuntimeCommand> commandPaletteRuntimeCommand(std::string_view text)
{
    if (commandPaletteRejectsShell(text)) {
        return std::nullopt;
    }
    const auto command = normalize(text);
    if (command == "play") return makeCommand(runtime::RuntimeCommandKind::PlaybackPlay);
    if (command == "pause") return makeCommand(runtime::RuntimeCommandKind::PlaybackPause);
    if (command == "resume") return makeCommand(runtime::RuntimeCommandKind::PlaybackResume);
    if (command == "toggle") return makeCommand(runtime::RuntimeCommandKind::PlaybackToggle);
    if (command == "stop") return makeCommand(runtime::RuntimeCommandKind::PlaybackStop);
    if (command == "next") return makeCommand(runtime::RuntimeCommandKind::QueueStep, runtime::RuntimeCommandPayload::queueStep(1));
    if (command == "prev" || command == "previous") return makeCommand(runtime::RuntimeCommandKind::QueueStep, runtime::RuntimeCommandPayload::queueStep(-1));
    if (command == "source reconnect" || command == "remote reconnect") return makeCommand(runtime::RuntimeCommandKind::RemoteReconnect);
    if (command == "eq on" || command == "eq enable") return makeCommand(runtime::RuntimeCommandKind::EqEnable);
    if (command == "eq off" || command == "eq disable") return makeCommand(runtime::RuntimeCommandKind::EqDisable);
    if (command == "eq reset") return makeCommand(runtime::RuntimeCommandKind::EqReset);
    if (command == "remix" || command == "effect cycle" || command == "audio effect cycle") return makeCommand(runtime::RuntimeCommandKind::AudioEffectCycle);
    return std::nullopt;
}

} // namespace lofibox::tui
