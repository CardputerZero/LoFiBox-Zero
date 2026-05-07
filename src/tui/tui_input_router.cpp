// SPDX-License-Identifier: GPL-3.0-or-later

#include "tui/tui_input_router.h"

#include <algorithm>
#include <utility>

#include "tui/tui_command_palette.h"

namespace lofibox::tui {
namespace {

runtime::RuntimeCommand runtimeCommand(runtime::RuntimeCommandKind kind, runtime::RuntimeCommandPayload payload = {})
{
    return runtime::RuntimeCommand{kind, std::move(payload), runtime::CommandOrigin::Automation, "tui:key"};
}

TuiInputAction commandAction(runtime::RuntimeCommand command)
{
    TuiInputAction action{};
    action.kind = TuiInputActionKind::RuntimeCommand;
    action.command = std::move(command);
    return action;
}

TuiInputAction viewAction(TuiViewMode mode)
{
    TuiInputAction action{};
    action.kind = TuiInputActionKind::ChangeView;
    action.view = mode;
    return action;
}

void eraseLastUtf8(std::string& value)
{
    if (value.empty()) {
        return;
    }
    value.pop_back();
    while (!value.empty() && (static_cast<unsigned char>(value.back()) & 0xC0U) == 0x80U) {
        value.pop_back();
    }
}

TuiInputAction routeTextInput(TuiModel& model, const TuiKeyEvent& event)
{
    auto& buffer = model.input_mode == TuiInputMode::Command ? model.command_buffer : model.search_buffer;
    if (event.key == TuiKey::Escape) {
        model.input_mode = TuiInputMode::Normal;
        return {};
    }
    if (event.key == TuiKey::Backspace) {
        eraseLastUtf8(buffer);
        return {};
    }
    if (event.key == TuiKey::CtrlU) {
        buffer.clear();
        return {};
    }
    if (event.key == TuiKey::Paste) {
        buffer += event.text;
        return {};
    }
    if (event.key == TuiKey::Character) {
        buffer += event.text;
        return {};
    }
    if (event.key == TuiKey::Enter) {
        if (model.input_mode == TuiInputMode::Command) {
            auto command = commandPaletteRuntimeCommand(buffer);
            buffer.clear();
            model.input_mode = TuiInputMode::Normal;
            if (command) {
                return commandAction(std::move(*command));
            }
            return {};
        }
        model.input_mode = TuiInputMode::Normal;
        return {};
    }
    return {};
}

} // namespace

TuiInputAction routeTuiInput(TuiModel& model, const TuiKeyEvent& event)
{
    if (model.input_mode == TuiInputMode::Search || model.input_mode == TuiInputMode::Command || model.input_mode == TuiInputMode::Text) {
        return routeTextInput(model, event);
    }
    if (event.key == TuiKey::Space) {
        return commandAction(runtimeCommand(runtime::RuntimeCommandKind::PlaybackToggle));
    }
    if (event.key == TuiKey::ArrowLeft || event.key == TuiKey::ShiftArrowLeft) {
        const double delta = event.key == TuiKey::ShiftArrowLeft ? 30.0 : 5.0;
        return commandAction(runtimeCommand(
            runtime::RuntimeCommandKind::PlaybackSeek,
            runtime::RuntimeCommandPayload::seek(std::max(0.0, model.snapshot.playback.elapsed_seconds - delta))));
    }
    if (event.key == TuiKey::ArrowRight || event.key == TuiKey::ShiftArrowRight) {
        const double delta = event.key == TuiKey::ShiftArrowRight ? 30.0 : 5.0;
        return commandAction(runtimeCommand(
            runtime::RuntimeCommandKind::PlaybackSeek,
            runtime::RuntimeCommandPayload::seek(model.snapshot.playback.elapsed_seconds + delta)));
    }
    if (event.key == TuiKey::Tab) {
        model.view = nextTuiViewMode(model.view);
        return viewAction(model.view);
    }
    if (event.key == TuiKey::BackTab) {
        model.view = previousTuiViewMode(model.view);
        return viewAction(model.view);
    }
    if (event.key == TuiKey::CtrlD) {
        model.scroll += 8;
        return {};
    }
    if (event.key == TuiKey::CtrlU) {
        model.scroll = std::max(0, model.scroll - 8);
        return {};
    }
    if (event.key == TuiKey::Escape) {
        model.input_mode = TuiInputMode::Normal;
        model.help_open = false;
        return {};
    }
    if (event.key != TuiKey::Character) {
        return {};
    }

    switch (event.codepoint) {
    case U'q':
        model.quit_requested = true;
        return TuiInputAction{TuiInputActionKind::Quit};
    case U'n':
        return commandAction(runtimeCommand(runtime::RuntimeCommandKind::QueueStep, runtime::RuntimeCommandPayload::queueStep(1)));
    case U'p':
        return commandAction(runtimeCommand(runtime::RuntimeCommandKind::QueueStep, runtime::RuntimeCommandPayload::queueStep(-1)));
    case U's':
        return commandAction(runtimeCommand(runtime::RuntimeCommandKind::PlaybackStop));
    case U'r':
        return commandAction(runtimeCommand(runtime::RuntimeCommandKind::RemoteReconnect));
    case U'R':
        return commandAction(runtimeCommand(runtime::RuntimeCommandKind::AudioEffectCycle));
    case U'e':
        return commandAction(runtimeCommand(model.snapshot.eq.enabled ? runtime::RuntimeCommandKind::EqDisable : runtime::RuntimeCommandKind::EqEnable));
    case U'l':
    case U'3':
        model.view = TuiViewMode::Lyrics;
        return viewAction(model.view);
    case U'v':
    case U'4':
        model.view = TuiViewMode::Spectrum;
        return viewAction(model.view);
    case U'Q':
    case U'5':
        model.view = TuiViewMode::Queue;
        return viewAction(model.view);
    case U'1':
        model.view = TuiViewMode::Dashboard;
        return viewAction(model.view);
    case U'2':
        model.view = TuiViewMode::NowPlaying;
        return viewAction(model.view);
    case U'8':
        model.view = TuiViewMode::Eq;
        return viewAction(model.view);
    case U'9':
        model.view = TuiViewMode::Diagnostics;
        return viewAction(model.view);
    case U'/':
        model.input_mode = TuiInputMode::Search;
        return TuiInputAction{TuiInputActionKind::OpenSearch};
    case U':':
        model.input_mode = TuiInputMode::Command;
        return TuiInputAction{TuiInputActionKind::OpenCommandPalette};
    case U'?':
        model.help_open = !model.help_open;
        return TuiInputAction{TuiInputActionKind::ToggleHelp};
    default:
        return {};
    }
}

} // namespace lofibox::tui
