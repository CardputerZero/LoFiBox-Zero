// SPDX-License-Identifier: GPL-3.0-or-later

#include "webui/webui_ws_runtime_stream.h"

#include <array>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include <sys/socket.h>
#include <unistd.h>

#include "webui/webui_projection.h"
#include "webui/webui_runtime_adapter.h"

namespace lofibox::webui {
namespace {

// WebSocket magic GUID per RFC 6455
constexpr std::string_view kWebSocketMagicGuid = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";

// WebSocket frame opcodes
constexpr std::uint8_t kOpText   = 0x1;
constexpr std::uint8_t kOpClose  = 0x8;

// Extract the value of an HTTP header from the raw request.
// Returns empty string_view if not found.
std::string_view extractHeader(std::string_view request, std::string_view header_name)
{
    // Search for "Header-Name: " case-insensitively by scanning lines
    std::size_t pos = 0;
    while (pos < request.size()) {
        auto line_end = request.find("\r\n", pos);
        if (line_end == std::string_view::npos) break;

        std::string_view line = request.substr(pos, line_end - pos);

        // Find colon separator
        auto colon = line.find(':');
        if (colon != std::string_view::npos) {
            std::string_view name = line.substr(0, colon);
            // Trim trailing whitespace from name
            while (!name.empty() && (name.back() == ' ' || name.back() == '\t')) {
                name.remove_suffix(1);
            }

            if (name.size() == header_name.size()) {
                // Case-insensitive compare
                bool match = true;
                for (std::size_t i = 0; i < name.size(); ++i) {
                    char a = name[i];
                    char b = header_name[i];
                    if (a >= 'A' && a <= 'Z') a += ('a' - 'A');
                    if (b >= 'A' && b <= 'Z') b += ('a' - 'A');
                    if (a != b) { match = false; break; }
                }
                if (match) {
                    // Skip ": "
                    std::size_t val_start = colon + 1;
                    while (val_start < line.size() && (line[val_start] == ' ' || line[val_start] == '\t')) ++val_start;
                    return line.substr(val_start);
                }
            }
        }

        pos = line_end + 2;
    }
    return {};
}

// --- Minimal SHA-1 (FIPS 180-4) ---

std::uint32_t rotl32(std::uint32_t x, int n)
{
    return (x << n) | (x >> (32 - n));
}

std::string sha1Hex(const void* data, std::size_t len)
{
    // SHA-1 constants
    constexpr std::uint32_t k[4] = {
        0x5A827999, 0x6ED9EBA1, 0x8F1BBCDC, 0xCA62C1D6
    };

    // Initial hash values
    std::uint32_t h[5] = {
        0x67452301, 0xEFCDAB89, 0x98BADCFE, 0x10325476, 0xC3D2E1F0
    };

    // Pre-processing: padding
    std::uint64_t bit_len = static_cast<std::uint64_t>(len) * 8;
    std::size_t padded_len = ((len + 8) / 64 + 1) * 64;
    if ((len + 8) % 64 == 0) padded_len = len + 8 + 64;

    std::vector<std::uint8_t> msg(padded_len, 0);
    std::memcpy(msg.data(), data, len);
    msg[len] = 0x80;

    // Append bit length as big-endian 64-bit
    for (int i = 0; i < 8; ++i) {
        msg[padded_len - 1 - i] = static_cast<std::uint8_t>(bit_len >> (i * 8));
    }

    // Process each 512-bit chunk
    for (std::size_t chunk = 0; chunk < padded_len; chunk += 64) {
        std::uint32_t w[80]{};

        for (int i = 0; i < 16; ++i) {
            std::size_t off = chunk + static_cast<std::size_t>(i) * 4;
            w[i] = (static_cast<std::uint32_t>(msg[off]) << 24)
                 | (static_cast<std::uint32_t>(msg[off + 1]) << 16)
                 | (static_cast<std::uint32_t>(msg[off + 2]) << 8)
                 | (static_cast<std::uint32_t>(msg[off + 3]));
        }

        for (int i = 16; i < 80; ++i) {
            w[i] = rotl32(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
        }

        std::uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4];

        for (int i = 0; i < 80; ++i) {
            std::uint32_t f = 0;
            std::uint32_t kt = 0;
            if (i < 20) {
                f = (b & c) | (~b & d);
                kt = k[0];
            } else if (i < 40) {
                f = b ^ c ^ d;
                kt = k[1];
            } else if (i < 60) {
                f = (b & c) | (b & d) | (c & d);
                kt = k[2];
            } else {
                f = b ^ c ^ d;
                kt = k[3];
            }

            std::uint32_t temp = rotl32(a, 5) + f + e + kt + w[i];
            e = d;
            d = c;
            c = rotl32(b, 30);
            b = a;
            a = temp;
        }

        h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e;
    }

    // Convert to hex
    std::ostringstream hex;
    hex << std::hex;
    for (int i = 0; i < 5; ++i) {
        for (int shift = 28; shift >= 0; shift -= 4) {
            hex << ((h[i] >> shift) & 0xF);
        }
    }
    return hex.str();
}

// --- Minimal Base64 ---

std::string base64Encode(const void* data, std::size_t len)
{
    constexpr char alphabet[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

    const auto* input = static_cast<const std::uint8_t*>(data);
    std::string out;
    out.reserve(((len + 2) / 3) * 4);

    for (std::size_t i = 0; i < len; i += 3) {
        std::uint32_t triple = static_cast<std::uint32_t>(input[i]) << 16;
        if (i + 1 < len) triple |= static_cast<std::uint32_t>(input[i + 1]) << 8;
        if (i + 2 < len) triple |= static_cast<std::uint32_t>(input[i + 2]);

        out += alphabet[(triple >> 18) & 0x3F];
        out += alphabet[(triple >> 12) & 0x3F];

        if (i + 1 < len) {
            out += alphabet[(triple >> 6) & 0x3F];
        } else {
            out += '=';
        }

        if (i + 2 < len) {
            out += alphabet[triple & 0x3F];
        } else {
            out += '=';
        }
    }
    return out;
}

} // namespace

WebUiWsRuntimeStream::WebUiWsRuntimeStream(int client_fd, WebUiRuntimeAdapter& adapter)
    : fd_(client_fd)
    , adapter_(adapter)
{
}

WebUiWsRuntimeStream::~WebUiWsRuntimeStream()
{
    stop();
    if (fd_ >= 0) {
        ::close(fd_);
        fd_ = -1;
    }
}

bool WebUiWsRuntimeStream::performUpgrade(std::string_view request_headers)
{
    std::string_view key = extractHeader(request_headers, "Sec-WebSocket-Key");
    if (key.empty()) return false;

    // Concatenate key + magic GUID
    std::string combined;
    combined.reserve(key.size() + kWebSocketMagicGuid.size());
    combined.append(key);
    combined.append(kWebSocketMagicGuid);

    // SHA-1 hash → binary (20 bytes)
    std::string sha1hex = sha1Hex(combined.data(), combined.size());

    // Convert hex to bytes
    std::array<std::uint8_t, 20> sha1bytes{};
    for (std::size_t i = 0; i < 20; ++i) {
        char high = sha1hex[i * 2];
        char low  = sha1hex[i * 2 + 1];
        auto hexVal = [](char c) -> std::uint8_t {
            if (c >= '0' && c <= '9') return static_cast<std::uint8_t>(c - '0');
            if (c >= 'a' && c <= 'f') return static_cast<std::uint8_t>(c - 'a' + 10);
            if (c >= 'A' && c <= 'F') return static_cast<std::uint8_t>(c - 'A' + 10);
            return 0;
        };
        sha1bytes[i] = static_cast<std::uint8_t>((hexVal(high) << 4) | hexVal(low));
    }

    std::string accept_key = base64Encode(sha1bytes.data(), sha1bytes.size());

    // Send HTTP 101 Switching Protocols response
    std::ostringstream response;
    response << "HTTP/1.1 101 Switching Protocols\r\n"
             << "Upgrade: websocket\r\n"
             << "Connection: Upgrade\r\n"
             << "Sec-WebSocket-Accept: " << accept_key << "\r\n"
             << "\r\n";

    std::string resp_str = response.str();
    ssize_t sent = ::send(fd_, resp_str.data(), resp_str.size(), 0);
    if (sent != static_cast<ssize_t>(resp_str.size())) return false;

    running_.store(true, std::memory_order_release);
    return true;
}

void WebUiWsRuntimeStream::run()
{
    // Prime the adapter with a snapshot so pollEvents has a baseline
    adapter_.querySnapshot();

    while (running_.load(std::memory_order_acquire)) {
        auto events = adapter_.pollEvents();

        for (const auto& event : events) {
            std::string json = buildEventJson(event);
            if (!sendFrame(json)) {
                // Client disconnected or send error
                running_.store(false, std::memory_order_release);
                return;
            }
        }

        // Poll interval: ~100ms for smooth spectrum/lyrics updates
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    sendClose();
}

void WebUiWsRuntimeStream::stop()
{
    running_.store(false, std::memory_order_release);
}

bool WebUiWsRuntimeStream::sendFrame(std::string_view payload)
{
    // Build WebSocket frame (server → client, unmasked)
    std::vector<std::uint8_t> frame;
    frame.reserve(2 + 8 + payload.size());

    // First byte: FIN + opcode (text)
    frame.push_back(0x80 | kOpText);

    // Second byte + extended length
    if (payload.size() <= 125) {
        frame.push_back(static_cast<std::uint8_t>(payload.size()));
    } else if (payload.size() <= 65535) {
        frame.push_back(126);
        frame.push_back(static_cast<std::uint8_t>(payload.size() >> 8));
        frame.push_back(static_cast<std::uint8_t>(payload.size() & 0xFF));
    } else {
        frame.push_back(127);
        for (int i = 7; i >= 0; --i) {
            frame.push_back(static_cast<std::uint8_t>(static_cast<std::uint64_t>(payload.size()) >> (i * 8)));
        }
    }

    // Payload
    frame.insert(frame.end(), payload.begin(), payload.end());

    ssize_t sent = ::send(fd_, frame.data(), frame.size(), 0);
    return sent == static_cast<ssize_t>(frame.size());
}

void WebUiWsRuntimeStream::sendClose()
{
    // Send close frame: FIN + close opcode, no payload
    std::uint8_t close_frame[] = { 0x88, 0x00 };
    ::send(fd_, close_frame, sizeof(close_frame), 0);
}

} // namespace lofibox::webui
