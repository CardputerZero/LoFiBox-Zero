// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <atomic>
#include <string>
#include <string_view>

namespace lofibox::webui {

class WebUiRuntimeAdapter;

// Manages a single WebSocket connection that pushes runtime events
// to the client as JSON text frames.
//
// Lifecycle:
//   1. Construct with a connected client socket fd and the runtime adapter.
//   2. Call performUpgrade() with the HTTP upgrade request headers.
//   3. If upgrade succeeds, call run() (blocks until connection closes or stop() is called).
//   4. The socket fd is closed on destruction.
class WebUiWsRuntimeStream {
public:
    WebUiWsRuntimeStream(int client_fd, WebUiRuntimeAdapter& adapter);
    ~WebUiWsRuntimeStream();

    WebUiWsRuntimeStream(const WebUiWsRuntimeStream&) = delete;
    WebUiWsRuntimeStream& operator=(const WebUiWsRuntimeStream&) = delete;

    // Attempt a WebSocket upgrade. Pass the raw HTTP request line + headers.
    // Returns true if the 101 response was sent successfully.
    bool performUpgrade(std::string_view request_headers);

    // Run the event push loop. Blocks until stop() is called or the socket closes.
    void run();

    // Signal the event loop to stop (thread-safe).
    void stop();

private:
    int fd_;
    WebUiRuntimeAdapter& adapter_;
    std::atomic<bool> running_{false};

    // Send a WebSocket text frame.
    bool sendFrame(std::string_view payload);

    // Send a WebSocket close frame and shutdown.
    void sendClose();
};

} // namespace lofibox::webui
