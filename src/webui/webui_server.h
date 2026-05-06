// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <atomic>
#include <memory>
#include <string>
#include <thread>

#include "webui/webui_config.h"

namespace lofibox::app {
class LibraryEnrichProvider;
class LibraryQueryProvider;
} // namespace lofibox::app

namespace lofibox::webui {

class WebUiHttpRouter;
class WebUiRuntimeAdapter;

// POSIX-socket HTTP + WebSocket server.
// Runs the accept loop on a background thread.  Each connection is handled
// synchronously (one at a time) to keep the implementation simple.
//
// Lifecycle:
//   WebUiServer server{config, adapter};
//   server.start();   // begins listening on config.bind_address:config.port
//   ... app runs ...
//   server.stop();    // shuts down the listener and joins the thread
class WebUiServer {
public:
    WebUiServer(WebUiConfig config, WebUiRuntimeAdapter& adapter);
    ~WebUiServer();

    WebUiServer(const WebUiServer&) = delete;
    WebUiServer& operator=(const WebUiServer&) = delete;

    // Set optional providers for /api/library/* endpoints.
    void setLibraryQueryProvider(app::LibraryQueryProvider* provider) noexcept;
    void setLibraryEnrichProvider(app::LibraryEnrichProvider* provider) noexcept;

    // Start the server. Returns false if socket setup fails.
    bool start();

    // Stop the server and join the background thread.
    void stop();

    // True while the accept loop is running.
    bool isRunning() const noexcept;

    // The address on which the server is listening (for logging).
    const std::string& bindAddress() const noexcept;
    std::uint16_t port() const noexcept;

private:
    // The accept loop (runs on a dedicated thread).
    void run();

    // Handle a single client connection (HTTP or WebSocket).
    void handleClient(int client_fd);

    // Read a full HTTP request from a socket.
    // Returns the raw bytes, or empty string on error.
    std::string readHttpRequest(int fd);

    WebUiConfig config_;
    WebUiRuntimeAdapter& adapter_;
    std::unique_ptr<WebUiHttpRouter> router_;
    int listen_fd_{-1};
    std::thread thread_{};
    std::atomic<bool> running_{false};
};

} // namespace lofibox::webui
