// SPDX-License-Identifier: GPL-3.0-or-later

#include "app/input_actions.h"

namespace lofibox::app {
UserAction mapInput(const InputEvent& event)
{
    switch (event.key) {
    case InputKey::Up:
        return UserAction::Up;
    case InputKey::Down:
        return UserAction::Down;
    case InputKey::Left:
        return UserAction::Left;
    case InputKey::Right:
        return UserAction::Right;
    case InputKey::Enter:
        return UserAction::Confirm;
    case InputKey::Backspace:
        return UserAction::Back;
    case InputKey::Delete:
    case InputKey::F1:
    case InputKey::F2:
    case InputKey::F3:
    case InputKey::F4:
    case InputKey::F5:
    case InputKey::F6:
    case InputKey::F7:
    case InputKey::F8:
    case InputKey::F9:
    case InputKey::F10:
    case InputKey::F11:
    case InputKey::F12:
    case InputKey::Insert:
        return UserAction::None;
    case InputKey::Home:
        return UserAction::Home;
    case InputKey::PageUp:
        return UserAction::PageUp;
    case InputKey::PageDown:
        return UserAction::PageDown;
    case InputKey::Next:
        return UserAction::NextTrack;
    case InputKey::Character:
        return UserAction::None;
    default:
        return UserAction::None;
    }
}

} // namespace lofibox::app
