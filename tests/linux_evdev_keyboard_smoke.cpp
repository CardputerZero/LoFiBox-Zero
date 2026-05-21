// SPDX-License-Identifier: GPL-3.0-or-later

#include <iostream>

#include <linux/input.h>

#include "platform/device/linux_evdev_keyboard.h"

namespace {

bool expectKey(unsigned int keycode, lofibox::app::InputKey expected, const char* label)
{
    const auto translated = lofibox::platform::device::translateLinuxEvdevCommandKey(keycode);
    if (!translated || translated->key != expected) {
        std::cerr << "Expected " << label << " to translate to the matching logical command key.\n";
        return false;
    }
    return true;
}

bool expectNoCommand(unsigned int keycode, const char* label)
{
    const auto translated = lofibox::platform::device::translateLinuxEvdevCommandKey(keycode);
    if (translated) {
        std::cerr << "Expected " << label << " to remain a character key, not a direction command.\n";
        return false;
    }
    return true;
}

} // namespace

int main()
{
    if (!expectKey(KEY_UP, lofibox::app::InputKey::Up, "KEY_UP")) return 1;
    if (!expectKey(KEY_DOWN, lofibox::app::InputKey::Down, "KEY_DOWN")) return 1;
    if (!expectKey(KEY_LEFT, lofibox::app::InputKey::Left, "KEY_LEFT")) return 1;
    if (!expectKey(KEY_RIGHT, lofibox::app::InputKey::Right, "KEY_RIGHT")) return 1;

    if (!expectNoCommand(KEY_X, "KEY_X")) return 1;
    if (!expectNoCommand(KEY_F, "KEY_F")) return 1;
    if (!expectNoCommand(KEY_S, "KEY_S")) return 1;
    if (!expectNoCommand(KEY_Z, "KEY_Z")) return 1;
    if (!expectNoCommand(KEY_C, "KEY_C")) return 1;

    return 0;
}
