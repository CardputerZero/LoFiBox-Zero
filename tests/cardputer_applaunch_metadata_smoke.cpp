// SPDX-License-Identifier: GPL-3.0-or-later

#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

#ifndef LOFIBOX_PROJECT_SOURCE_DIR
#error "LOFIBOX_PROJECT_SOURCE_DIR must be defined"
#endif

namespace {

std::string readTextFile(const std::filesystem::path& path)
{
    std::ifstream input(path);
    if (!input) {
        throw std::runtime_error("Unable to open " + path.string());
    }
    return std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
}

bool hasLine(const std::string& text, const std::string& expected)
{
    std::size_t start = 0;
    while (start <= text.size()) {
        const auto end = text.find('\n', start);
        auto line = text.substr(start, end == std::string::npos ? std::string::npos : end - start);
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (line == expected) {
            return true;
        }
        if (end == std::string::npos) {
            break;
        }
        start = end + 1;
    }
    return false;
}

std::uint32_t readBigEndian32(const std::array<unsigned char, 24>& bytes, std::size_t offset)
{
    return (static_cast<std::uint32_t>(bytes[offset]) << 24)
        | (static_cast<std::uint32_t>(bytes[offset + 1]) << 16)
        | (static_cast<std::uint32_t>(bytes[offset + 2]) << 8)
        | static_cast<std::uint32_t>(bytes[offset + 3]);
}

struct PngDimensions {
    std::uint32_t width{};
    std::uint32_t height{};
};

PngDimensions readPngDimensions(const std::filesystem::path& path)
{
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw std::runtime_error("Unable to open " + path.string());
    }

    std::array<unsigned char, 24> header{};
    input.read(reinterpret_cast<char*>(header.data()), static_cast<std::streamsize>(header.size()));
    if (input.gcount() != static_cast<std::streamsize>(header.size())) {
        throw std::runtime_error("PNG header is too short: " + path.string());
    }

    constexpr std::array<unsigned char, 8> png_signature{{137, 80, 78, 71, 13, 10, 26, 10}};
    for (std::size_t i = 0; i < png_signature.size(); ++i) {
        if (header[i] != png_signature[i]) {
            throw std::runtime_error("File is not a PNG: " + path.string());
        }
    }
    if (header[12] != 'I' || header[13] != 'H' || header[14] != 'D' || header[15] != 'R') {
        throw std::runtime_error("PNG does not start with an IHDR chunk: " + path.string());
    }

    return {readBigEndian32(header, 16), readBigEndian32(header, 20)};
}

} // namespace

int main()
{
    const auto project_root = std::filesystem::path{LOFIBOX_PROJECT_SOURCE_DIR};
    const auto desktop_file = project_root / "data" / "lofibox-cardputer-zero.desktop";
    const auto wrapper_file = project_root / "data" / "lofibox-cardputer-zero-applaunch";
    const auto icon_file = project_root / "assets" / "ui" / "icons" / "cardputer-zero" / "lofibox.png";

    try {
        const auto desktop = readTextFile(desktop_file);
        if (!hasLine(desktop, "Exec=/usr/lib/lofibox/lofibox-applaunch")) {
            std::cerr << "Expected Cardputer APPLaunch metadata to start the installed wrapper.\n";
            return 1;
        }
        if (!hasLine(desktop, "Icon=share/images/lofibox.png")) {
            std::cerr << "Expected Cardputer APPLaunch metadata to use the APPLaunch-relative icon path.\n";
            return 1;
        }
        if (!hasLine(desktop, "StartupWMClass=lofibox")) {
            std::cerr << "Expected Cardputer APPLaunch metadata to expose a stable window class for task matching.\n";
            return 1;
        }
        if (!hasLine(desktop, "X-Zero-AppId=io.github.vicliu624.lofibox")) {
            std::cerr << "Expected Cardputer APPLaunch metadata to expose the LoFiBox Wayland app id.\n";
            return 1;
        }
        if (!hasLine(desktop, "X-Zero-Display=wayland")) {
            std::cerr << "Expected Cardputer APPLaunch metadata to declare the native Wayland runtime.\n";
            return 1;
        }

        const auto wrapper = readTextFile(wrapper_file);
        if (!hasLine(wrapper, "find_st7789v_fbdev()")) {
            std::cerr << "Expected Cardputer APPLaunch wrapper to be able to detect the ST7789V framebuffer.\n";
            return 1;
        }
        if (!hasLine(wrapper, ": \"${LOFIBOX_WEBUI:=1}\"")
            || !hasLine(wrapper, ": \"${LOFIBOX_WEBUI_BIND:=0.0.0.0}\"")
            || !hasLine(wrapper, ": \"${LOFIBOX_WEBUI_PORT:=8765}\"")
            || !hasLine(wrapper, "export LOFIBOX_WEBUI LOFIBOX_WEBUI_BIND LOFIBOX_WEBUI_PORT")) {
            std::cerr << "Expected Cardputer APPLaunch wrapper to enable the appliance WebUI by default.\n";
            return 1;
        }
        if (!hasLine(wrapper, "    wayland|labwc)")) {
            std::cerr << "Expected Cardputer APPLaunch wrapper to support an explicit Wayland backend.\n";
            return 1;
        }
        if (!hasLine(wrapper, "    fb|fbdev|framebuffer|device)")) {
            std::cerr << "Expected Cardputer APPLaunch wrapper to support an explicit framebuffer backend.\n";
            return 1;
        }
        if (!hasLine(wrapper, "            exec lofibox-wayland \"$@\"")) {
            std::cerr << "Expected Cardputer APPLaunch wrapper to start the native desktop runtime in Wayland sessions.\n";
            return 1;
        }
        if (!hasLine(wrapper, "            *fb_st7789v*)")) {
            std::cerr << "Expected Cardputer APPLaunch wrapper to detect fb_st7789v from /proc/fb.\n";
            return 1;
        }
        if (!hasLine(wrapper, "        LOFIBOX_FBDEV=$APPLAUNCH_LINUX_FBDEV_DEVICE")) {
            std::cerr << "Expected Cardputer APPLaunch wrapper to reuse APPLaunch's detected framebuffer.\n";
            return 1;
        }
        if (!hasLine(wrapper, "        LOFIBOX_FBDEV=$detected_fbdev")) {
            std::cerr << "Expected Cardputer APPLaunch wrapper to use its own framebuffer detection fallback.\n";
            return 1;
        }
        if (!hasLine(wrapper, "        LOFIBOX_FBDEV=/dev/fb1")) {
            std::cerr << "Expected Cardputer APPLaunch wrapper to fall back to the known small-screen framebuffer.\n";
            return 1;
        }
        if (!hasLine(wrapper, "    LOFIBOX_INPUT_DEV=\"${APPLAUNCH_LINUX_KEYBOARD_DEVICE:-/dev/input/by-path/platform-3f804000.i2c-event}\"")) {
            std::cerr << "Expected Cardputer APPLaunch wrapper to reuse APPLaunch's keyboard device.\n";
            return 1;
        }
        if (!hasLine(wrapper, "export LOFIBOX_FBDEV LOFIBOX_INPUT_DEV")) {
            std::cerr << "Expected Cardputer APPLaunch wrapper to export device environment variables.\n";
            return 1;
        }
        if (!hasLine(wrapper, "exec \"$script_dir/lofibox_zero_device\" \"$@\"")) {
            std::cerr << "Expected Cardputer APPLaunch wrapper to exec the installed device binary.\n";
            return 1;
        }

        const auto dimensions = readPngDimensions(icon_file);
        if (dimensions.width > 81 || dimensions.height > 81) {
            std::cerr << "Expected Cardputer APPLaunch icon to fit the 81x81 side panel, got "
                      << dimensions.width << "x" << dimensions.height << ".\n";
            return 1;
        }
    } catch (const std::exception& error) {
        std::cerr << error.what() << "\n";
        return 1;
    }

    return 0;
}
