// SPDX-License-Identifier: GPL-3.0-or-later

#include "webui/webui_server.h"

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <memory>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include "webui/webui_http_router.h"
#include "webui/webui_json.h"
#include "webui/webui_runtime_adapter.h"
#include "webui/webui_ws_runtime_stream.h"

namespace lofibox::webui {
namespace {

constexpr int kListenBacklog = 4;
constexpr std::size_t kReadBufferSize = 4096;

// Set a socket to non-blocking mode.
bool setNonBlocking(int fd)
{
    int flags = ::fcntl(fd, F_GETFL, 0);
    if (flags < 0) return false;
    return ::fcntl(fd, F_SETFL, flags | O_NONBLOCK) >= 0;
}

// Parse the HTTP method from the request line (e.g. "GET /path HTTP/1.1").
std::string_view parseMethod(std::string_view request_line)
{
    auto space = request_line.find(' ');
    if (space == std::string_view::npos) return {};
    return request_line.substr(0, space);
}

// Parse the URL path from the request line.
std::string_view parsePath(std::string_view request_line)
{
    auto start = request_line.find(' ');
    if (start == std::string_view::npos) return {};
    ++start;
    auto end = request_line.find(' ', start);
    if (end == std::string_view::npos) return {};
    return request_line.substr(start, end - start);
}

// Check if the request is a WebSocket upgrade.
bool isWebSocketUpgrade(std::string_view headers)
{
    // Look for "Upgrade: websocket" header
    return headers.find("Upgrade: websocket") != std::string_view::npos
        || headers.find("upgrade: websocket") != std::string_view::npos;
}

} // namespace

WebUiServer::WebUiServer(WebUiConfig config, WebUiRuntimeAdapter& adapter)
    : config_(std::move(config))
    , adapter_(adapter)
    , router_(std::make_unique<WebUiHttpRouter>(adapter_))
{
}

WebUiServer::~WebUiServer()
{
    stop();
}

void WebUiServer::setLibraryQueryProvider(app::LibraryQueryProvider* provider) noexcept
{
    if (router_) {
        router_->setLibraryQueryProvider(provider);
    }
}

void WebUiServer::setLibraryEnrichProvider(app::LibraryEnrichProvider* provider) noexcept
{
    if (router_) {
        router_->setLibraryEnrichProvider(provider);
    }
}

void WebUiServer::setTheme(const ui::UiTheme* theme) noexcept
{
    if (router_) {
        router_->setTheme(theme);
    }
}

bool WebUiServer::start()
{
    if (running_.load(std::memory_order_acquire)) return true;

    listen_fd_ = ::socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (listen_fd_ < 0) return false;

    // Set SO_REUSEADDR to avoid "address in use" errors on restart
    int opt = 1;
    ::setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = ::htons(config_.port);
    if (::inet_pton(AF_INET, config_.bind_address.c_str(), &addr.sin_addr) != 1) {
        ::close(listen_fd_);
        listen_fd_ = -1;
        return false;
    }

    if (::bind(listen_fd_, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0) {
        ::close(listen_fd_);
        listen_fd_ = -1;
        return false;
    }

    if (::listen(listen_fd_, kListenBacklog) < 0) {
        ::close(listen_fd_);
        listen_fd_ = -1;
        return false;
    }

    running_.store(true, std::memory_order_release);
    thread_ = std::thread(&WebUiServer::run, this);
    return true;
}

void WebUiServer::stop()
{
    running_.store(false, std::memory_order_release);

    // Close the listen socket to unblock accept()
    if (listen_fd_ >= 0) {
        ::shutdown(listen_fd_, SHUT_RDWR);
        ::close(listen_fd_);
        listen_fd_ = -1;
    }

    if (thread_.joinable()) {
        thread_.join();
    }
}

bool WebUiServer::isRunning() const noexcept
{
    return running_.load(std::memory_order_acquire);
}

const std::string& WebUiServer::bindAddress() const noexcept
{
    return config_.bind_address;
}

std::uint16_t WebUiServer::port() const noexcept
{
    return config_.port;
}

void WebUiServer::run()
{
    while (running_.load(std::memory_order_acquire)) {
        struct sockaddr_in client_addr{};
        socklen_t client_len = sizeof(client_addr);
        int client_fd = ::accept4(listen_fd_, reinterpret_cast<struct sockaddr*>(&client_addr), &client_len, SOCK_CLOEXEC);

        if (client_fd < 0) {
            if (errno == EINTR) continue;
            if (!running_.load(std::memory_order_acquire)) break;
            continue;
        }

        handleClient(client_fd);
    }
}

void WebUiServer::handleClient(int client_fd)
{
    std::string request = readHttpRequest(client_fd);
    if (request.empty()) {
        ::close(client_fd);
        return;
    }

    // Parse the request line
    auto line_end = request.find("\r\n");
    if (line_end == std::string_view::npos) {
        ::close(client_fd);
        return;
    }
    std::string_view request_line{request.data(), line_end};
    std::string_view headers{request.data(), request.size()};

    std::string_view method = parseMethod(request_line);
    std::string_view path = parsePath(request_line);

    // Check for WebSocket upgrade
    if (isWebSocketUpgrade(headers)) {
        auto ws = std::make_shared<WebUiWsRuntimeStream>(client_fd, adapter_);
        if (ws->performUpgrade(headers)) {
            // Run the WebSocket event loop on its own thread so the
            // accept loop can continue handling concurrent HTTP requests.
            std::thread([ws]() { ws->run(); }).detach();
        } else {
            ::close(client_fd);
        }
        return;
    }

    // Extract body from request (after \r\n\r\n)
    auto body_start = request.find("\r\n\r\n");
    std::string_view body;
    if (body_start != std::string::npos) {
        body_start += 4;
        body = std::string_view{request}.substr(body_start);
    }

    // Command dispatch and enrichment may perform network I/O (Emby, Wikipedia).
    // Handle on a separate thread so they don't block the accept loop.
    if ((method == "POST" && path == "/api/runtime/commands")
        || (method == "GET" && (path.starts_with("/api/library/artist/") || path.starts_with("/api/library/album/")))) {
        int fd = client_fd;
        std::string req = std::move(request);
        std::thread([this, fd, req = std::move(req)]() {
            auto line_end = req.find("\r\n");
            std::string_view request_line{req.data(), line_end};
            std::string_view method_sv = parseMethod(request_line);
            std::string_view path_sv = parsePath(request_line);
            auto body_start = req.find("\r\n\r\n");
            std::string_view body_sv;
            if (body_start != std::string::npos) body_sv = std::string_view{req}.substr(body_start + 4);
            std::string resp = router_->handleRequest(method_sv, path_sv, body_sv);
            const char* data = resp.data();
            std::size_t remaining = resp.size();
            while (remaining > 0) {
                ssize_t sent = ::send(fd, data, remaining, MSG_NOSIGNAL);
                if (sent < 0) break;
                data += sent;
                remaining -= static_cast<std::size_t>(sent);
            }
            ::close(fd);
        }).detach();
        return;
    }

    std::string response = router_->handleRequest(method, path, body);

    // Send response (loop for large payloads like artwork PNGs)
    {
        const char* data = response.data();
        std::size_t remaining = response.size();
        while (remaining > 0) {
            ssize_t sent = ::send(client_fd, data, remaining, MSG_NOSIGNAL);
            if (sent < 0) break;
            data += sent;
            remaining -= static_cast<std::size_t>(sent);
        }
    }
    ::close(client_fd);
}

std::string WebUiServer::readHttpRequest(int fd)
{
    std::string buffer;
    buffer.reserve(kReadBufferSize);

    char chunk[kReadBufferSize];
    ssize_t n;

    // Read until we have the full headers (\r\n\r\n)
    while ((n = ::recv(fd, chunk, sizeof(chunk), 0)) > 0) {
        buffer.append(chunk, static_cast<std::size_t>(n));
        if (buffer.find("\r\n\r\n") != std::string::npos) {
            break;
        }
        if (buffer.size() > 65536) {
            return {}; // Too large, reject
        }
    }

    if (n < 0) {
        return {};
    }

    return buffer;
}

} // namespace lofibox::webui
