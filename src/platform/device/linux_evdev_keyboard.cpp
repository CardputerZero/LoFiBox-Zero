// SPDX-License-Identifier: GPL-3.0-or-later

#include "platform/device/linux_evdev_keyboard.h"

#include <array>
#include <cerrno>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>

#include <chrono>
#include <fcntl.h>
#include <linux/input.h>
#include <unistd.h>
#include <xkbcommon/xkbcommon.h>

namespace lofibox::platform::device {
namespace {

struct CustomCharacterKey {
    std::uint32_t keycode{};
    char text{};
};

constexpr std::array<CustomCharacterKey, 31> kTca8418CharacterKeys{{
    {183U, '!'},
    {184U, '@'},
    {185U, '#'},
    {186U, '$'},
    {187U, '%'},
    {188U, '^'},
    {189U, '&'},
    {190U, '*'},
    {191U, '('},
    {192U, ')'},
    {193U, '~'},
    {194U, '`'},
    {195U, '+'},
    {196U, '-'},
    {197U, '/'},
    {198U, '\\'},
    {199U, '{'},
    {200U, '}'},
    {201U, '['},
    {202U, ']'},
    {231U, ','},
    {209U, '='},
    {210U, ':'},
    {211U, ';'},
    {212U, '_'},
    {213U, '?'},
    {214U, '<'},
    {215U, '>'},
    {216U, '\''},
    {217U, '"'},
    {232U, '.'},
}};

constexpr std::uint32_t kEvdevKeycodeOffset = 8U;

[[nodiscard]] std::optional<char> lookupCustomCharacter(std::uint32_t keycode) noexcept
{
    for (const auto& mapping : kTca8418CharacterKeys) {
        if (mapping.keycode == keycode) {
            return mapping.text;
        }
    }

    return std::nullopt;
}

[[nodiscard]] bool isPrintableAscii(std::uint32_t codepoint) noexcept
{
    return codepoint == static_cast<std::uint32_t>(' ')
        || (codepoint >= 33U && codepoint <= 126U);
}

[[nodiscard]] std::string characterLabel(char ch)
{
    if (ch == ' ') {
        return "SPACE";
    }

    std::string label(1, ch);
    if (std::isalpha(static_cast<unsigned char>(ch))) {
        label[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
    }

    return label;
}

[[nodiscard]] app::InputEvent makeCharacterEvent(char ch)
{
    return app::makeCharacterInput(ch, characterLabel(ch));
}

[[nodiscard]] std::optional<app::InputEvent> translateSpecialKey(std::uint32_t keycode)
{
    switch (keycode) {
    case KEY_Z:
        return app::InputEvent{app::InputKey::Left, "LEFT", '\0'};
    case KEY_X:
        return app::InputEvent{app::InputKey::Down, "DOWN", '\0'};
    case KEY_C:
        return app::InputEvent{app::InputKey::Right, "RIGHT", '\0'};
    case KEY_S:
        return app::InputEvent{app::InputKey::Up, "UP", '\0'};
    case KEY_BACKSPACE:
        return app::InputEvent{app::InputKey::Backspace, "BACK", '\0'};
    case KEY_DELETE:
        return app::InputEvent{app::InputKey::Delete, "DEL", '\0'};
    case KEY_ENTER:
        return app::InputEvent{app::InputKey::Enter, "OK", '\0'};
    case KEY_TAB:
        return app::InputEvent{app::InputKey::Tab, "TAB", '\0'};
    case KEY_F1:
        return app::InputEvent{app::InputKey::F1, "F1", '\0'};
    case KEY_F2:
        return app::InputEvent{app::InputKey::F2, "F2", '\0'};
    case KEY_F3:
        return app::InputEvent{app::InputKey::F3, "F3", '\0'};
    case KEY_F4:
        return app::InputEvent{app::InputKey::F4, "F4", '\0'};
    case KEY_F5:
        return app::InputEvent{app::InputKey::F5, "F5", '\0'};
    case KEY_F6:
        return app::InputEvent{app::InputKey::F6, "F6", '\0'};
    case KEY_F7:
        return app::InputEvent{app::InputKey::F7, "F7", '\0'};
    case KEY_F8:
        return app::InputEvent{app::InputKey::F8, "F8", '\0'};
    case KEY_F9:
        return app::InputEvent{app::InputKey::F9, "F9", '\0'};
    case KEY_F10:
        return app::InputEvent{app::InputKey::F10, "F10", '\0'};
    case KEY_F11:
        return app::InputEvent{app::InputKey::F11, "F11", '\0'};
    case KEY_F12:
        return app::InputEvent{app::InputKey::F12, "F12", '\0'};
    case KEY_HOME:
        return app::InputEvent{app::InputKey::Home, "HOME", '\0'};
    case KEY_PAGEUP:
        return app::InputEvent{app::InputKey::PageUp, "PGUP", '\0'};
    case KEY_PAGEDOWN:
        return app::InputEvent{app::InputKey::PageDown, "PGDN", '\0'};
    case KEY_INSERT:
        return app::InputEvent{app::InputKey::Insert, "INS", '\0'};
    case KEY_LEFT:
        return app::InputEvent{app::InputKey::Left, "LEFT", '\0'};
    case KEY_RIGHT:
        return app::InputEvent{app::InputKey::Right, "RIGHT", '\0'};
    case KEY_UP:
        return app::InputEvent{app::InputKey::Up, "UP", '\0'};
    case KEY_DOWN:
        return app::InputEvent{app::InputKey::Down, "DOWN", '\0'};
    case KEY_LEFTSHIFT:
    case KEY_RIGHTSHIFT:
        return app::InputEvent{app::InputKey::Shift, "AA", '\0'};
    case KEY_LEFTCTRL:
    case KEY_RIGHTCTRL:
        return app::InputEvent{app::InputKey::Ctrl, "CTRL", '\0'};
    case KEY_LEFTALT:
    case KEY_RIGHTALT:
        return app::InputEvent{app::InputKey::Alt, "ALT", '\0'};
    default:
        break;
    }

#ifdef KEY_CAPSLOCK
    if (keycode == KEY_CAPSLOCK) {
        return app::InputEvent{app::InputKey::Shift, "AA", '\0'};
    }
#endif

#ifdef KEY_NEXT
    if (keycode == KEY_NEXT) {
        return app::InputEvent{app::InputKey::Next, "NEXT", '\0'};
    }
#endif

#ifdef KEY_POWER
    if (keycode == KEY_POWER) {
        return app::InputEvent{app::InputKey::Power, "POWER", '\0'};
    }
#endif

#ifdef KEY_FN
    if (keycode == KEY_FN) {
        return app::InputEvent{app::InputKey::Fn, "FN", '\0'};
    }
#endif

    return std::nullopt;
}

[[nodiscard]] bool updatesXkbState(std::uint32_t keycode) noexcept
{
    switch (keycode) {
    case KEY_LEFTSHIFT:
    case KEY_RIGHTSHIFT:
    case KEY_LEFTCTRL:
    case KEY_RIGHTCTRL:
    case KEY_LEFTALT:
    case KEY_RIGHTALT:
        return true;
    default:
        break;
    }

#ifdef KEY_CAPSLOCK
    if (keycode == KEY_CAPSLOCK) {
        return true;
    }
#endif

    return false;
}

void updateXkbState(struct xkb_state* state, std::uint32_t keycode, int value) noexcept
{
    if (!state || !updatesXkbState(keycode) || value == 2) {
        return;
    }

    const auto direction = value == 0 ? XKB_KEY_UP : XKB_KEY_DOWN;
    xkb_state_update_key(state, keycode + kEvdevKeycodeOffset, direction);
}

[[nodiscard]] std::optional<char> translateAscii(struct xkb_state* state, std::uint32_t keycode) noexcept
{
    if (!state) {
        return std::nullopt;
    }

    const std::uint32_t codepoint = xkb_state_key_get_utf32(state, keycode + kEvdevKeycodeOffset);
    if (!isPrintableAscii(codepoint)) {
        return std::nullopt;
    }

    return static_cast<char>(codepoint);
}

} // namespace

struct LinuxEvdevKeyboard::Impl {
    explicit Impl(std::string device_path_in, std::string xkb_layout_in)
        : device_path(std::move(device_path_in))
        , xkb_layout(std::move(xkb_layout_in))
    {
        fd = ::open(device_path.c_str(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
        if (fd < 0) {
            std::cerr << "Device input disabled: " << device_path << ": " << std::strerror(errno) << '\n';
            return;
        }

        context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
        if (!context) {
            throw std::runtime_error("xkb_context_new failed");
        }

        xkb_rule_names names{};
        names.rules = "evdev";
        names.layout = xkb_layout.c_str();

        keymap = xkb_keymap_new_from_names(context, &names, XKB_KEYMAP_COMPILE_NO_FLAGS);
        if (!keymap) {
            throw std::runtime_error("xkb_keymap_new_from_names failed");
        }

        state = xkb_state_new(keymap);
        if (!state) {
            throw std::runtime_error("xkb_state_new failed");
        }
    }

    ~Impl()
    {
        if (state) {
            xkb_state_unref(state);
        }
        if (keymap) {
            xkb_keymap_unref(keymap);
        }
        if (context) {
            xkb_context_unref(context);
        }
        if (fd >= 0) {
            ::close(fd);
        }
    }

    [[nodiscard]] std::vector<app::InputEvent> drainInput()
    {
        std::vector<app::InputEvent> drained{};
        if (fd < 0) {
            return drained;
        }

        input_event event{};
        while (true) {
            const ssize_t bytes_read = ::read(fd, &event, sizeof(event));
            if (bytes_read < 0) {
                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    break;
                }

                throw std::runtime_error(std::string("keyboard read failed: ") + std::strerror(errno));
            }

            if (bytes_read == 0) {
                break;
            }

            if (bytes_read != static_cast<ssize_t>(sizeof(event))) {
                continue;
            }

            if (event.type != EV_KEY) {
                continue;
            }

            const std::uint32_t keycode = static_cast<std::uint32_t>(event.code);
            const int value = event.value;

            if (keycode == KEY_ESC) {
                if (value == 1) {
                    esc_press_time_ = std::chrono::steady_clock::now();
                } else if (value == 0) {
                    esc_press_time_ = {};
                }
            }

            if (value == 0) {
                updateXkbState(state, keycode, value);
                continue;
            }

            if (value != 1 && value != 2) {
                continue;
            }

            if (const auto special = translateSpecialKey(keycode)) {
                updateXkbState(state, keycode, value);
                drained.push_back(*special);
                continue;
            }

            if (const auto custom = lookupCustomCharacter(keycode)) {
                drained.push_back(makeCharacterEvent(*custom));
                continue;
            }

            if (const auto ascii = translateAscii(state, keycode)) {
                drained.push_back(makeCharacterEvent(*ascii));
            }
        }

        return drained;
    }

    std::string device_path{};
    std::string xkb_layout{};
    int fd{-1};
    xkb_context* context{};
    xkb_keymap* keymap{};
    xkb_state* state{};
    std::chrono::steady_clock::time_point esc_press_time_{};
};

LinuxEvdevKeyboard::LinuxEvdevKeyboard(std::string device_path, std::string xkb_layout)
    : impl_(std::make_unique<Impl>(std::move(device_path), std::move(xkb_layout)))
{
}

LinuxEvdevKeyboard::~LinuxEvdevKeyboard() = default;

bool LinuxEvdevKeyboard::available() const noexcept
{
    return impl_ && impl_->fd >= 0;
}

std::chrono::milliseconds LinuxEvdevKeyboard::escHeldDuration() const noexcept
{
    if (!impl_ || impl_->esc_press_time_ == std::chrono::steady_clock::time_point{}) {
        return std::chrono::milliseconds::zero();
    }
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - impl_->esc_press_time_);
}

std::vector<app::InputEvent> LinuxEvdevKeyboard::drainInput()
{
    return impl_->drainInput();
}

} // namespace lofibox::platform::device
