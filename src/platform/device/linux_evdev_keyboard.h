// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "app/input_event.h"

namespace lofibox::platform::device {

[[nodiscard]] std::optional<app::InputEvent> translateLinuxEvdevCommandKey(std::uint32_t keycode);

class LinuxEvdevKeyboard {
public:
    LinuxEvdevKeyboard(std::string device_path, std::string xkb_layout = "us");
    ~LinuxEvdevKeyboard();

    LinuxEvdevKeyboard(const LinuxEvdevKeyboard&) = delete;
    LinuxEvdevKeyboard& operator=(const LinuxEvdevKeyboard&) = delete;

    [[nodiscard]] bool available() const noexcept;
    [[nodiscard]] std::vector<app::InputEvent> drainInput();
    [[nodiscard]] std::chrono::milliseconds escHeldDuration() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_{};
};

} // namespace lofibox::platform::device
