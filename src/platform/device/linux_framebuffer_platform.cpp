// SPDX-License-Identifier: GPL-3.0-or-later

#include "platform/device/linux_framebuffer_platform.h"
#include "platform/device/linux_evdev_keyboard.h"
#include "platform/device/linux_framebuffer.h"

#include <algorithm>
#include <atomic>
#include <csignal>
#include <stdexcept>
#include <utility>

#include <linux/fb.h>

namespace lofibox::platform::device {
namespace {

std::atomic_bool g_running{true};

void stopSignalHandler(int)
{
    g_running.store(false);
}

std::uint32_t scaleChannel(std::uint8_t value, const fb_bitfield& bitfield) noexcept
{
    if (bitfield.length == 0U) {
        return 0U;
    }

    const std::uint32_t max_value = (1U << bitfield.length) - 1U;
    const std::uint32_t scaled = (static_cast<std::uint32_t>(value) * max_value + 127U) / 255U;
    return scaled << bitfield.offset;
}

std::uint32_t packNativePixel(core::Color color, const fb_var_screeninfo& vinfo) noexcept
{
    return scaleChannel(color.r, vinfo.red)
        | scaleChannel(color.g, vinfo.green)
        | scaleChannel(color.b, vinfo.blue)
        | scaleChannel(color.a, vinfo.transp);
}

} // namespace

struct LinuxFramebufferPlatform::Impl {
    Impl(std::string framebuffer_path_in, std::string input_device_path_in)
        : framebuffer(std::move(framebuffer_path_in))
        , keyboard(std::move(input_device_path_in))
    {
        std::signal(SIGINT, stopSignalHandler);
        std::signal(SIGTERM, stopSignalHandler);

        bytes_per_pixel = static_cast<int>(framebuffer.variableInfo().bits_per_pixel / 8U);
        if (bytes_per_pixel < 2 || bytes_per_pixel > 4) {
            throw std::runtime_error("unsupported framebuffer pixel depth");
        }
    }

    void writePackedPixel(int x, int y, std::uint32_t packed) const noexcept
    {
        const auto& vinfo = framebuffer.variableInfo();
        const auto& finfo = framebuffer.fixedInfo();
        const int pixel_x = x + static_cast<int>(vinfo.xoffset);
        const int pixel_y = y + static_cast<int>(vinfo.yoffset);
        const std::size_t offset = static_cast<std::size_t>(pixel_y) * static_cast<std::size_t>(finfo.line_length)
            + static_cast<std::size_t>(pixel_x) * static_cast<std::size_t>(bytes_per_pixel);

        for (int byte_index = 0; byte_index < bytes_per_pixel; ++byte_index) {
            framebuffer.framebufferMemory()[offset + static_cast<std::size_t>(byte_index)] =
                static_cast<std::byte>((packed >> (byte_index * 8)) & 0xFFU);
        }
    }

    LinuxFramebuffer framebuffer;
    LinuxEvdevKeyboard keyboard;
    int bytes_per_pixel{};
};

LinuxFramebufferPlatform::LinuxFramebufferPlatform(std::string framebuffer_path, std::string input_device_path)
    : impl_(std::make_unique<Impl>(std::move(framebuffer_path), std::move(input_device_path)))
{
}

LinuxFramebufferPlatform::~LinuxFramebufferPlatform() = default;

bool LinuxFramebufferPlatform::pump()
{
    if (impl_->keyboard.escHeldDuration() >= std::chrono::milliseconds{2000}) {
        g_running.store(false);
    }
    return g_running.load();
}

std::vector<app::InputEvent> LinuxFramebufferPlatform::drainInput()
{
    return impl_->keyboard.drainInput();
}

void LinuxFramebufferPlatform::present(const core::Canvas& canvas)
{
    const auto& vinfo = impl_->framebuffer.variableInfo();
    const int physical_width = static_cast<int>(vinfo.xres);
    const int physical_height = static_cast<int>(vinfo.yres);
    const int scale_x = physical_width / canvas.width();
    const int scale_y = physical_height / canvas.height();
    const int scale = std::min(scale_x, scale_y);

    if (scale <= 0) {
        throw std::runtime_error("framebuffer is smaller than the logical display");
    }

    const int render_width = canvas.width() * scale;
    const int render_height = canvas.height() * scale;
    const int offset_x = (physical_width - render_width) / 2;
    const int offset_y = (physical_height - render_height) / 2;

    impl_->framebuffer.clear();

    for (int y = 0; y < canvas.height(); ++y) {
        for (int x = 0; x < canvas.width(); ++x) {
            const std::uint32_t packed = packNativePixel(canvas.pixel(x, y), vinfo);

            for (int dy = 0; dy < scale; ++dy) {
                for (int dx = 0; dx < scale; ++dx) {
                    impl_->writePackedPixel(offset_x + (x * scale) + dx, offset_y + (y * scale) + dy, packed);
                }
            }
        }
    }
}

} // namespace lofibox::platform::device
