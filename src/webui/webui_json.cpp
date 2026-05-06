// SPDX-License-Identifier: GPL-3.0-or-later

#include "webui/webui_json.h"

namespace lofibox::webui::json {

std::string escape(std::string_view text)
{
    std::string out;
    out.reserve(text.size() + 4);
    for (const char ch : text) {
        switch (ch) {
        case '\\': out += "\\\\"; break;
        case '"':  out += "\\\""; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default:   out += ch; break;
        }
    }
    return out;
}

void separator(std::ostringstream& out)
{
    out << ',';
}

void appendString(std::ostringstream& out, std::string_view key, std::string_view value)
{
    out << '"' << key << "\":\"" << escape(value) << '"';
}

void appendBool(std::ostringstream& out, std::string_view key, bool value)
{
    out << '"' << key << "\":" << (value ? "true" : "false");
}

void appendInt(std::ostringstream& out, std::string_view key, int value)
{
    out << '"' << key << "\":" << value;
}

void appendInt64(std::ostringstream& out, std::string_view key, std::int64_t value)
{
    out << '"' << key << "\":" << value;
}

void appendDouble(std::ostringstream& out, std::string_view key, double value)
{
    out << '"' << key << "\":" << value;
}

void openObject(std::ostringstream& out, std::string_view key)
{
    out << '"' << key << "\":{";
}

void closeObject(std::ostringstream& out)
{
    out << '}';
}

void openArray(std::ostringstream& out, std::string_view key)
{
    out << '"' << key << "\":[";
}

void closeArray(std::ostringstream& out)
{
    out << ']';
}

void appendStringArray(std::ostringstream& out, std::string_view key, const std::vector<std::string>& values)
{
    openArray(out, key);
    for (std::size_t index = 0; index < values.size(); ++index) {
        if (index > 0) out << ',';
        out << '"' << escape(values[index]) << '"';
    }
    closeArray(out);
}

void appendIntArray(std::ostringstream& out, std::string_view key, const std::vector<int>& values)
{
    openArray(out, key);
    for (std::size_t index = 0; index < values.size(); ++index) {
        if (index > 0) out << ',';
        out << values[index];
    }
    closeArray(out);
}

std::string httpOk(std::string_view content_type, std::size_t content_length)
{
    std::ostringstream out;
    out << "HTTP/1.1 200 OK\r\n"
        << "Content-Type: " << content_type << "\r\n"
        << "Content-Length: " << content_length << "\r\n"
        << "Connection: keep-alive\r\n"
        << "Access-Control-Allow-Origin: *\r\n"
        << "\r\n";
    return out.str();
}

std::string httpBadRequest(std::string_view message)
{
    std::ostringstream out;
    out << "HTTP/1.1 400 Bad Request\r\n"
        << "Content-Type: text/plain\r\n"
        << "Content-Length: " << message.size() << "\r\n"
        << "Connection: keep-alive\r\n"
        << "Access-Control-Allow-Origin: *\r\n"
        << "\r\n"
        << message;
    return out.str();
}

std::string httpNotFound()
{
    constexpr std::string_view body = "Not Found";
    std::ostringstream out;
    out << "HTTP/1.1 404 Not Found\r\n"
        << "Content-Type: text/plain\r\n"
        << "Content-Length: " << body.size() << "\r\n"
        << "Connection: keep-alive\r\n"
        << "Access-Control-Allow-Origin: *\r\n"
        << "\r\n"
        << body;
    return out.str();
}

std::string httpMethodNotAllowed()
{
    constexpr std::string_view body = "Method Not Allowed";
    std::ostringstream out;
    out << "HTTP/1.1 405 Method Not Allowed\r\n"
        << "Content-Type: text/plain\r\n"
        << "Content-Length: " << body.size() << "\r\n"
        << "Connection: keep-alive\r\n"
        << "Access-Control-Allow-Origin: *\r\n"
        << "\r\n"
        << body;
    return out.str();
}

std::string httpServerError(std::string_view message)
{
    std::ostringstream out;
    out << "HTTP/1.1 500 Internal Server Error\r\n"
        << "Content-Type: text/plain\r\n"
        << "Content-Length: " << message.size() << "\r\n"
        << "Connection: close\r\n"
        << "Access-Control-Allow-Origin: *\r\n"
        << "\r\n"
        << message;
    return out.str();
}

} // namespace lofibox::webui::json
