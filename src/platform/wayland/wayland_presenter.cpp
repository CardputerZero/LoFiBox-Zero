// SPDX-License-Identifier: GPL-3.0-or-later

#include "platform/wayland/wayland_presenter.h"

#include "xdg-decoration-unstable-v1-client-protocol.h"
#include "xdg-shell-client-protocol.h"

#include <algorithm>
#include <cerrno>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <poll.h>
#include <stdexcept>
#include <string>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <utility>

#include <wayland-client.h>
#include <xkbcommon/xkbcommon.h>

#include "core/display_profile.h"

#ifndef MFD_CLOEXEC
#define MFD_CLOEXEC 0x0001U
#endif

namespace lofibox::platform::wayland {
namespace {

constexpr int kWidth = core::kDisplayWidth;
constexpr int kHeight = core::kDisplayHeight;
constexpr int kStride = kWidth * 4;
constexpr int kBufferSize = kStride * kHeight;

int createAnonymousFile(std::size_t size)
{
    int fd = -1;
#ifdef SYS_memfd_create
    fd = static_cast<int>(syscall(SYS_memfd_create, "lofibox-wayland", MFD_CLOEXEC));
    if (fd >= 0) {
        if (ftruncate(fd, static_cast<off_t>(size)) == 0) {
            return fd;
        }
        close(fd);
    }
#endif

    char name[] = "/lofibox-wayland-XXXXXX";
    fd = shm_open(name, O_RDWR | O_CREAT | O_EXCL, 0600);
    if (fd < 0) {
        return -1;
    }
    shm_unlink(name);
    if (ftruncate(fd, static_cast<off_t>(size)) < 0) {
        close(fd);
        return -1;
    }
    return fd;
}

[[nodiscard]] std::uint32_t packPixel(core::Color color) noexcept
{
    return 0xFF000000u
        | (static_cast<std::uint32_t>(color.r) << 16U)
        | (static_cast<std::uint32_t>(color.g) << 8U)
        | static_cast<std::uint32_t>(color.b);
}

[[nodiscard]] app::InputEvent makeChar(char ch)
{
    std::string label(1, ch);
    return app::makeCharacterInput(ch, std::move(label));
}

[[nodiscard]] app::InputEvent makeCommittedText(std::string text)
{
    return app::makeCommittedTextInput(std::move(text));
}

[[nodiscard]] std::vector<app::InputEvent> translateKeysym(xkb_keysym_t sym)
{
    switch (sym) {
    case XKB_KEY_BackSpace:
        return {app::InputEvent{app::InputKey::Backspace, "BACK", '\0'}};
    case XKB_KEY_Delete:
        return {app::InputEvent{app::InputKey::Delete, "DEL", '\0'}};
    case XKB_KEY_Return:
    case XKB_KEY_KP_Enter:
        return {app::InputEvent{app::InputKey::Enter, "OK", '\0'}};
    case XKB_KEY_Tab:
        return {app::InputEvent{app::InputKey::Tab, "TAB", '\0'}};
    case XKB_KEY_F1:
        return {app::InputEvent{app::InputKey::F1, "F1", '\0'}};
    case XKB_KEY_F2:
        return {app::InputEvent{app::InputKey::F2, "F2", '\0'}};
    case XKB_KEY_F3:
        return {app::InputEvent{app::InputKey::F3, "F3", '\0'}};
    case XKB_KEY_F4:
        return {app::InputEvent{app::InputKey::F4, "F4", '\0'}};
    case XKB_KEY_F5:
        return {app::InputEvent{app::InputKey::F5, "F5", '\0'}};
    case XKB_KEY_F6:
        return {app::InputEvent{app::InputKey::F6, "F6", '\0'}};
    case XKB_KEY_F7:
        return {app::InputEvent{app::InputKey::F7, "F7", '\0'}};
    case XKB_KEY_F8:
        return {app::InputEvent{app::InputKey::F8, "F8", '\0'}};
    case XKB_KEY_F9:
        return {app::InputEvent{app::InputKey::F9, "F9", '\0'}};
    case XKB_KEY_F10:
        return {app::InputEvent{app::InputKey::F10, "F10", '\0'}};
    case XKB_KEY_F11:
        return {app::InputEvent{app::InputKey::F11, "F11", '\0'}};
    case XKB_KEY_F12:
        return {app::InputEvent{app::InputKey::F12, "F12", '\0'}};
    case XKB_KEY_Home:
        return {app::InputEvent{app::InputKey::Home, "HOME", '\0'}};
    case XKB_KEY_Page_Up:
        return {app::InputEvent{app::InputKey::PageUp, "PGUP", '\0'}};
    case XKB_KEY_Page_Down:
        return {app::InputEvent{app::InputKey::PageDown, "PGDN", '\0'}};
    case XKB_KEY_Insert:
        return {app::InputEvent{app::InputKey::Insert, "INS", '\0'}};
    case XKB_KEY_Left:
        return {app::InputEvent{app::InputKey::Left, "LEFT", '\0'}};
    case XKB_KEY_Right:
        return {app::InputEvent{app::InputKey::Right, "RIGHT", '\0'}};
    case XKB_KEY_Up:
        return {app::InputEvent{app::InputKey::Up, "UP", '\0'}};
    case XKB_KEY_Down:
        return {app::InputEvent{app::InputKey::Down, "DOWN", '\0'}};
    default:
        return {};
    }
}

} // namespace

struct WaylandPresenter::Impl {
    struct Buffer {
        wl_buffer* buffer{};
        std::uint32_t* pixels{};
        bool busy{false};
    };

    Impl()
    {
        display = wl_display_connect(nullptr);
        if (display == nullptr) {
            throw std::runtime_error("wl_display_connect failed");
        }

        registry = wl_display_get_registry(display);
        wl_registry_add_listener(registry, &kRegistryListener, this);
        wl_display_roundtrip(display);
        wl_display_roundtrip(display);

        if (compositor == nullptr || shm == nullptr || wm == nullptr) {
            throw std::runtime_error("Wayland compositor is missing wl_compositor, wl_shm, or xdg_wm_base");
        }

        surface = wl_compositor_create_surface(compositor);
        if (surface == nullptr) {
            throw std::runtime_error("wl_compositor_create_surface failed");
        }

        xdg_surface_ = xdg_wm_base_get_xdg_surface(wm, surface);
        xdg_surface_add_listener(xdg_surface_, &kSurfaceListener, this);
        toplevel_ = xdg_surface_get_toplevel(xdg_surface_);
        xdg_toplevel_add_listener(toplevel_, &kToplevelListener, this);
        xdg_toplevel_set_title(toplevel_, "LoFiBox Zero");
        xdg_toplevel_set_app_id(toplevel_, "io.github.vicliu624.lofibox");
        xdg_toplevel_set_min_size(toplevel_, kWidth, kHeight);
        xdg_toplevel_set_max_size(toplevel_, kWidth, kHeight);
        if (decoration_manager_ != nullptr) {
            decoration_ = zxdg_decoration_manager_v1_get_toplevel_decoration(decoration_manager_, toplevel_);
            zxdg_toplevel_decoration_v1_set_mode(
                decoration_,
                ZXDG_TOPLEVEL_DECORATION_V1_MODE_CLIENT_SIDE);
        }

        if (!createBuffer(buffers[0]) || !createBuffer(buffers[1])) {
            throw std::runtime_error("Wayland shm buffer allocation failed");
        }

        wl_surface_commit(surface);
        wl_display_flush(display);
    }

    ~Impl()
    {
        for (auto& buffer : buffers) {
            if (buffer.buffer != nullptr) {
                wl_buffer_destroy(buffer.buffer);
            }
            if (buffer.pixels != nullptr) {
                munmap(buffer.pixels, kBufferSize);
            }
        }
        if (xkb_state_ != nullptr) {
            xkb_state_unref(xkb_state_);
        }
        if (xkb_keymap_ != nullptr) {
            xkb_keymap_unref(xkb_keymap_);
        }
        if (xkb_context_ != nullptr) {
            xkb_context_unref(xkb_context_);
        }
        if (keyboard != nullptr) {
            wl_keyboard_destroy(keyboard);
        }
        if (seat != nullptr) {
            wl_seat_destroy(seat);
        }
        if (decoration_ != nullptr) {
            zxdg_toplevel_decoration_v1_destroy(decoration_);
        }
        if (toplevel_ != nullptr) {
            xdg_toplevel_destroy(toplevel_);
        }
        if (xdg_surface_ != nullptr) {
            xdg_surface_destroy(xdg_surface_);
        }
        if (surface != nullptr) {
            wl_surface_destroy(surface);
        }
        if (wm != nullptr) {
            xdg_wm_base_destroy(wm);
        }
        if (decoration_manager_ != nullptr) {
            zxdg_decoration_manager_v1_destroy(decoration_manager_);
        }
        if (shm != nullptr) {
            wl_shm_destroy(shm);
        }
        if (compositor != nullptr) {
            wl_compositor_destroy(compositor);
        }
        if (registry != nullptr) {
            wl_registry_destroy(registry);
        }
        if (display != nullptr) {
            wl_display_disconnect(display);
        }
    }

    bool createBuffer(Buffer& buffer)
    {
        const int fd = createAnonymousFile(kBufferSize);
        if (fd < 0) {
            return false;
        }

        void* mapped = mmap(nullptr, kBufferSize, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
        if (mapped == MAP_FAILED) {
            close(fd);
            return false;
        }

        wl_shm_pool* pool = wl_shm_create_pool(shm, fd, kBufferSize);
        buffer.buffer = wl_shm_pool_create_buffer(pool, 0, kWidth, kHeight, kStride, WL_SHM_FORMAT_XRGB8888);
        buffer.pixels = static_cast<std::uint32_t*>(mapped);
        buffer.busy = false;
        wl_buffer_add_listener(buffer.buffer, &kBufferListener, &buffer);
        wl_shm_pool_destroy(pool);
        close(fd);
        return buffer.buffer != nullptr;
    }

    [[nodiscard]] Buffer* nextBuffer()
    {
        for (auto& buffer : buffers) {
            if (!buffer.busy) {
                return &buffer;
            }
        }
        return nullptr;
    }

    void updateKeymap(int fd, std::uint32_t size)
    {
        char* map = static_cast<char*>(mmap(nullptr, size, PROT_READ, MAP_PRIVATE, fd, 0));
        if (map == MAP_FAILED) {
            close(fd);
            return;
        }

        if (xkb_context_ == nullptr) {
            xkb_context_ = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
        }
        if (xkb_keymap_ != nullptr) {
            xkb_keymap_unref(xkb_keymap_);
        }
        if (xkb_state_ != nullptr) {
            xkb_state_unref(xkb_state_);
        }

        xkb_keymap_ = xkb_keymap_new_from_string(
            xkb_context_,
            map,
            XKB_KEYMAP_FORMAT_TEXT_V1,
            XKB_KEYMAP_COMPILE_NO_FLAGS);
        xkb_state_ = xkb_keymap_ != nullptr ? xkb_state_new(xkb_keymap_) : nullptr;

        munmap(map, size);
        close(fd);
    }

    void handleKey(std::uint32_t key, std::uint32_t state)
    {
        if (state != WL_KEYBOARD_KEY_STATE_PRESSED || xkb_state_ == nullptr) {
            return;
        }

        const auto keycode = static_cast<xkb_keycode_t>(key + 8U);
        const xkb_keysym_t sym = xkb_state_key_get_one_sym(xkb_state_, keycode);
        auto translated = translateKeysym(sym);
        if (!translated.empty()) {
            pending_inputs.insert(pending_inputs.end(), translated.begin(), translated.end());
            return;
        }

        char utf8[64] = {};
        const int len = xkb_state_key_get_utf8(xkb_state_, keycode, utf8, sizeof(utf8));
        if (len == 1 && static_cast<unsigned char>(utf8[0]) >= 0x20U && static_cast<unsigned char>(utf8[0]) <= 0x7EU) {
            pending_inputs.push_back(makeChar(utf8[0]));
        } else if (len > 0) {
            pending_inputs.push_back(makeCommittedText(std::string{utf8, static_cast<std::size_t>(len)}));
        }
    }

    static void registryGlobal(void* data, wl_registry* registry, std::uint32_t name,
                               const char* interface, std::uint32_t version)
    {
        auto* self = static_cast<Impl*>(data);
        if (std::strcmp(interface, wl_compositor_interface.name) == 0) {
            self->compositor = static_cast<wl_compositor*>(
                wl_registry_bind(registry, name, &wl_compositor_interface, std::min(version, 4U)));
        } else if (std::strcmp(interface, wl_shm_interface.name) == 0) {
            self->shm = static_cast<wl_shm*>(wl_registry_bind(registry, name, &wl_shm_interface, 1));
        } else if (std::strcmp(interface, wl_seat_interface.name) == 0) {
            self->seat = static_cast<wl_seat*>(
                wl_registry_bind(registry, name, &wl_seat_interface, std::min(version, 7U)));
            wl_seat_add_listener(self->seat, &kSeatListener, self);
        } else if (std::strcmp(interface, xdg_wm_base_interface.name) == 0) {
            self->wm = static_cast<xdg_wm_base*>(wl_registry_bind(registry, name, &xdg_wm_base_interface, 1));
            xdg_wm_base_add_listener(self->wm, &kWmListener, self);
        } else if (std::strcmp(interface, zxdg_decoration_manager_v1_interface.name) == 0) {
            self->decoration_manager_ = static_cast<zxdg_decoration_manager_v1*>(
                wl_registry_bind(registry, name, &zxdg_decoration_manager_v1_interface, 1));
        }
    }

    static void registryRemove(void*, wl_registry*, std::uint32_t) {}

    static void wmPing(void*, xdg_wm_base* wm, std::uint32_t serial)
    {
        xdg_wm_base_pong(wm, serial);
    }

    static void surfaceConfigure(void* data, xdg_surface* surface, std::uint32_t serial)
    {
        auto* self = static_cast<Impl*>(data);
        xdg_surface_ack_configure(surface, serial);
        self->configured = true;
    }

    static void toplevelConfigure(void*, xdg_toplevel*, std::int32_t, std::int32_t, wl_array*) {}

    static void toplevelClose(void* data, xdg_toplevel*)
    {
        static_cast<Impl*>(data)->running = false;
    }

    static void seatCapabilities(void* data, wl_seat*, std::uint32_t capabilities)
    {
        auto* self = static_cast<Impl*>(data);
        if ((capabilities & WL_SEAT_CAPABILITY_KEYBOARD) != 0U && self->keyboard == nullptr) {
            self->keyboard = wl_seat_get_keyboard(self->seat);
            wl_keyboard_add_listener(self->keyboard, &kKeyboardListener, self);
        } else if ((capabilities & WL_SEAT_CAPABILITY_KEYBOARD) == 0U && self->keyboard != nullptr) {
            wl_keyboard_destroy(self->keyboard);
            self->keyboard = nullptr;
        }
    }

    static void seatName(void*, wl_seat*, const char*) {}

    static void keyboardKeymap(void* data, wl_keyboard*, std::uint32_t format, std::int32_t fd, std::uint32_t size)
    {
        if (format != WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1) {
            close(fd);
            return;
        }
        static_cast<Impl*>(data)->updateKeymap(fd, size);
    }

    static void keyboardEnter(void*, wl_keyboard*, std::uint32_t, wl_surface*, wl_array*) {}
    static void keyboardLeave(void*, wl_keyboard*, std::uint32_t, wl_surface*) {}

    static void keyboardKey(void* data, wl_keyboard*, std::uint32_t, std::uint32_t,
                            std::uint32_t key, std::uint32_t state)
    {
        static_cast<Impl*>(data)->handleKey(key, state);
    }

    static void keyboardModifiers(void* data, wl_keyboard*, std::uint32_t, std::uint32_t depressed,
                                  std::uint32_t latched, std::uint32_t locked, std::uint32_t group)
    {
        auto* self = static_cast<Impl*>(data);
        if (self->xkb_state_ != nullptr) {
            xkb_state_update_mask(self->xkb_state_, depressed, latched, locked, 0, 0, group);
        }
    }

    static void keyboardRepeat(void*, wl_keyboard*, std::int32_t, std::int32_t) {}

    static void bufferRelease(void* data, wl_buffer*)
    {
        static_cast<Buffer*>(data)->busy = false;
    }

    static const wl_registry_listener kRegistryListener;
    static const xdg_wm_base_listener kWmListener;
    static const xdg_surface_listener kSurfaceListener;
    static const xdg_toplevel_listener kToplevelListener;
    static const wl_seat_listener kSeatListener;
    static const wl_keyboard_listener kKeyboardListener;
    static const wl_buffer_listener kBufferListener;

    wl_display* display{};
    wl_registry* registry{};
    wl_compositor* compositor{};
    wl_shm* shm{};
    wl_seat* seat{};
    wl_keyboard* keyboard{};
    wl_surface* surface{};
    xdg_wm_base* wm{};
    xdg_surface* xdg_surface_{};
    xdg_toplevel* toplevel_{};
    zxdg_decoration_manager_v1* decoration_manager_{};
    zxdg_toplevel_decoration_v1* decoration_{};
    xkb_context* xkb_context_{};
    xkb_keymap* xkb_keymap_{};
    xkb_state* xkb_state_{};
    Buffer buffers[2]{};
    bool configured{false};
    bool running{true};
    std::vector<app::InputEvent> pending_inputs{};
};

const wl_registry_listener WaylandPresenter::Impl::kRegistryListener{
    WaylandPresenter::Impl::registryGlobal,
    WaylandPresenter::Impl::registryRemove,
};

const xdg_wm_base_listener WaylandPresenter::Impl::kWmListener{
    WaylandPresenter::Impl::wmPing,
};

const xdg_surface_listener WaylandPresenter::Impl::kSurfaceListener{
    WaylandPresenter::Impl::surfaceConfigure,
};

const xdg_toplevel_listener WaylandPresenter::Impl::kToplevelListener{
    WaylandPresenter::Impl::toplevelConfigure,
    WaylandPresenter::Impl::toplevelClose,
    nullptr,
    nullptr,
};

const wl_seat_listener WaylandPresenter::Impl::kSeatListener{
    WaylandPresenter::Impl::seatCapabilities,
    WaylandPresenter::Impl::seatName,
};

const wl_keyboard_listener WaylandPresenter::Impl::kKeyboardListener{
    WaylandPresenter::Impl::keyboardKeymap,
    WaylandPresenter::Impl::keyboardEnter,
    WaylandPresenter::Impl::keyboardLeave,
    WaylandPresenter::Impl::keyboardKey,
    WaylandPresenter::Impl::keyboardModifiers,
    WaylandPresenter::Impl::keyboardRepeat,
};

const wl_buffer_listener WaylandPresenter::Impl::kBufferListener{
    WaylandPresenter::Impl::bufferRelease,
};

WaylandPresenter::WaylandPresenter()
    : impl_(std::make_unique<Impl>())
{
}

WaylandPresenter::~WaylandPresenter() = default;

bool WaylandPresenter::pump()
{
    while (wl_display_prepare_read(impl_->display) != 0) {
        wl_display_dispatch_pending(impl_->display);
    }
    wl_display_flush(impl_->display);

    pollfd pfd{};
    pfd.fd = wl_display_get_fd(impl_->display);
    pfd.events = POLLIN;
    const int rc = poll(&pfd, 1, 0);
    if (rc > 0 && (pfd.revents & POLLIN) != 0) {
        wl_display_read_events(impl_->display);
        wl_display_dispatch_pending(impl_->display);
    } else {
        wl_display_cancel_read(impl_->display);
        wl_display_dispatch_pending(impl_->display);
    }

    return impl_->running && wl_display_get_error(impl_->display) == 0;
}

std::vector<app::InputEvent> WaylandPresenter::drainInput()
{
    auto result = std::move(impl_->pending_inputs);
    impl_->pending_inputs.clear();
    return result;
}

void WaylandPresenter::present(const core::Canvas& canvas)
{
    if (!impl_->configured) {
        return;
    }

    auto* buffer = impl_->nextBuffer();
    if (buffer == nullptr) {
        return;
    }

    for (int y = 0; y < canvas.height(); ++y) {
        for (int x = 0; x < canvas.width(); ++x) {
            buffer->pixels[y * kWidth + x] = packPixel(canvas.pixel(x, y));
        }
    }

    wl_surface_attach(impl_->surface, buffer->buffer, 0, 0);
    wl_surface_damage_buffer(impl_->surface, 0, 0, kWidth, kHeight);
    wl_surface_commit(impl_->surface);
    buffer->busy = true;
    wl_display_flush(impl_->display);
}

} // namespace lofibox::platform::wayland
