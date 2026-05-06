// SPDX-License-Identifier: GPL-3.0-or-later

#include "webui/webui_http_router.h"

#include <algorithm>
#include <charconv>
#include <cstdlib>
#include <cstring>
#include <sstream>
#include <string>

#include "app/library_enrich_provider.h"
#include "app/library_query_provider.h"
#include "runtime/runtime_result.h"
#include "webui/webui_json.h"
#include "webui/webui_projection.h"
#include "webui/webui_runtime_adapter.h"
#include "webui/webui_static_assets.h"
#include "webui/webui_theme_tokens.h"

namespace lofibox::webui {
namespace {

// --- Minimal JSON value extraction for flat command objects ---

// Extract a JSON string value for a given key from a flat JSON object.
// Returns the unescaped value, or empty string_view if not found.
std::string_view extractJsonString(std::string_view body, std::string_view key)
{
    // Search for "key":
    std::string search = "\"";
    search += key;
    search += "\":";
    auto pos = body.find(search);
    if (pos == std::string_view::npos) return {};

    pos += search.size();
    // Skip whitespace
    while (pos < body.size() && (body[pos] == ' ' || body[pos] == '\t' || body[pos] == '\n' || body[pos] == '\r')) ++pos;
    if (pos >= body.size() || body[pos] != '"') return {};

    ++pos; // skip opening quote
    auto end = body.find('"', pos);
    if (end == std::string_view::npos) return {};

    return body.substr(pos, end - pos);
}

// Extract a JSON integer value for a given key.
// Returns the parsed value and sets found=true if the key exists.
int extractJsonInt(std::string_view body, std::string_view key, bool& found)
{
    found = false;
    std::string search = "\"";
    search += key;
    search += "\":";
    auto pos = body.find(search);
    if (pos == std::string_view::npos) return 0;

    pos += search.size();
    // Skip whitespace
    while (pos < body.size() && (body[pos] == ' ' || body[pos] == '\t' || body[pos] == '\n' || body[pos] == '\r')) ++pos;
    if (pos >= body.size()) return 0;

    // Handle negative numbers
    bool negative = false;
    if (body[pos] == '-') {
        negative = true;
        ++pos;
    }

    if (pos >= body.size() || body[pos] < '0' || body[pos] > '9') return 0;

    int value = 0;
    while (pos < body.size() && body[pos] >= '0' && body[pos] <= '9') {
        value = value * 10 + (body[pos] - '0');
        ++pos;
    }

    found = true;
    return negative ? -value : value;
}

// Minimal percent-decoding for URL path segments.
std::string urlDecode(std::string_view input)
{
    std::string out;
    out.reserve(input.size());
    for (std::size_t i = 0; i < input.size(); ++i) {
        if (input[i] == '%' && i + 2 < input.size()) {
            char hi = input[i + 1];
            char lo = input[i + 2];
            auto hex = [](char c) -> int {
                if (c >= '0' && c <= '9') return c - '0';
                if (c >= 'A' && c <= 'F') return c - 'A' + 10;
                if (c >= 'a' && c <= 'f') return c - 'a' + 10;
                return -1;
            };
            int h = hex(hi), l = hex(lo);
            if (h >= 0 && l >= 0) {
                out += static_cast<char>((h << 4) | l);
                i += 2;
                continue;
            }
        } else if (input[i] == '+') {
            out += ' ';
            continue;
        }
        out += input[i];
    }
    return out;
}

// Simple JSON string unescaping.
std::string unescapeJson(std::string_view input)
{
    std::string out;
    out.reserve(input.size());
    for (std::size_t i = 0; i < input.size(); ++i) {
        if (input[i] == '\\' && i + 1 < input.size()) {
            switch (input[++i]) {
            case '"':  out += '"'; break;
            case '\\': out += '\\'; break;
            case '/':  out += '/'; break;
            case 'n':  out += '\n'; break;
            case 'r':  out += '\r'; break;
            case 't':  out += '\t'; break;
            default:   out += input[i]; break;
            }
        } else {
            out += input[i];
        }
    }
    return out;
}

// Map a WebUI action name to RuntimeCommandKind + populate payload.
// Returns true if the action was recognized.
bool mapActionToCommand(std::string_view action, runtime::RuntimeCommand& command)
{
    using runtime::RuntimeCommandKind;
    using runtime::RuntimeCommandPayload;

    if (action == "Toggle") {
        command.kind = RuntimeCommandKind::PlaybackToggle;
        command.payload = RuntimeCommandPayload::empty();
        return true;
    }
    if (action == "Previous") {
        command.kind = RuntimeCommandKind::QueueStep;
        command.payload = RuntimeCommandPayload::queueStep(-1);
        return true;
    }
    if (action == "Next") {
        command.kind = RuntimeCommandKind::QueueStep;
        command.payload = RuntimeCommandPayload::queueStep(1);
        return true;
    }
    if (action == "ToggleShuffle") {
        command.kind = RuntimeCommandKind::PlaybackToggleShuffle;
        command.payload = RuntimeCommandPayload::empty();
        return true;
    }
    if (action == "CycleRepeat") {
        command.kind = RuntimeCommandKind::PlaybackCycleRepeat;
        command.payload = RuntimeCommandPayload::empty();
        return true;
    }
    if (action == "EqReset") {
        command.kind = RuntimeCommandKind::EqReset;
        command.payload = RuntimeCommandPayload::empty();
        return true;
    }

    // Commands that carry parameters are parsed by the caller
    return false;
}

// Build a result JSON for the command response.
std::string buildCommandResultJson(const runtime::RuntimeCommandResult& result)
{
    std::ostringstream out;
    out << '{';
    json::appendBool(out, "accepted", result.accepted);
    json::separator(out); json::appendBool(out, "applied", result.applied);
    json::separator(out); json::appendString(out, "code", result.code);
    json::separator(out); json::appendString(out, "message", result.message);
    json::separator(out); json::appendInt64(out, "version_before", static_cast<std::int64_t>(result.version_before_apply));
    json::separator(out); json::appendInt64(out, "version_after", static_cast<std::int64_t>(result.version_after_apply));
    out << '}';
    return out.str();
}

} // namespace

WebUiHttpRouter::WebUiHttpRouter(WebUiRuntimeAdapter& adapter) noexcept
    : adapter_(adapter)
{
}

void WebUiHttpRouter::setLibraryQueryProvider(app::LibraryQueryProvider* provider) noexcept
{
    library_provider_ = provider;
}

void WebUiHttpRouter::setLibraryEnrichProvider(app::LibraryEnrichProvider* provider) noexcept
{
    enrich_provider_ = provider;
}

void WebUiHttpRouter::setTheme(const ui::UiTheme* theme) noexcept
{
    theme_ = theme;
}

std::string WebUiHttpRouter::handleRequest(std::string_view method, std::string_view path, std::string_view body)
{
    // CORS preflight
    if (method == "OPTIONS") {
        std::ostringstream out;
        out << "HTTP/1.1 204 No Content\r\n"
            << "Access-Control-Allow-Origin: *\r\n"
            << "Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n"
            << "Access-Control-Allow-Headers: Content-Type\r\n"
            << "Content-Length: 0\r\n"
            << "\r\n";
        return out.str();
    }

    // GET /api/theme.css: theme CSS custom properties
    if (method == "GET" && path == "/api/theme.css") {
        std::string css = theme_ ? buildThemeCss(*theme_) : buildThemeCss(ui::defaultTheme());
        std::string response = json::httpOk("text/css; charset=utf-8", css.size());
        response += css;
        return response;
    }

    // GET / or /index.html: serve the SPA
    if (method == "GET" && (path == "/" || path == "/index.html")) {
        const char* html = webUiIndexHtml();
        const std::size_t len = std::strlen(html);
        std::string response = json::httpOk("text/html; charset=utf-8", len);
        response.append(html, len);
        return response;
    }

    // GET /api/runtime/snapshot: full state
    if (method == "GET" && path == "/api/runtime/snapshot") {
        adapter_.querySnapshot();
        std::string json = buildFullSnapshotJson(adapter_.lastSnapshot());
        std::string response = json::httpOk("application/json; charset=utf-8", json.size());
        response += json;
        return response;
    }

    // POST /api/runtime/commands: execute a command
    if (method == "POST" && path == "/api/runtime/commands") {
        runtime::RuntimeCommand command{};
        if (!parseWebUiCommand(body, command)) {
            std::ostringstream err;
            err << "{\"accepted\":false,\"code\":\"bad_request\",\"message\":\"Unable to parse command body\"}";
            std::string err_str = err.str();
            return json::httpBadRequest(err_str);
        }

        auto result = adapter_.submitCommand(command);
        std::string result_json = buildCommandResultJson(result);
        std::string response = json::httpOk("application/json; charset=utf-8", result_json.size());
        response += result_json;
        return response;
    }

    // Library content endpoints delegated to the injected provider interface.
    if (method == "GET" && library_provider_) {
        // GET /api/artwork/<track_id>: image/png binary
        constexpr std::string_view kArtworkPrefix = "/api/artwork/";
        if (path.starts_with(kArtworkPrefix) && path.size() > kArtworkPrefix.size()) {
            int track_id = 0;
            const auto id_str = path.substr(kArtworkPrefix.size());
            auto [ptr, ec] = std::from_chars(id_str.data(), id_str.data() + id_str.size(), track_id);
            if (ec == std::errc{} && track_id > 0) {
                auto png = library_provider_->getArtworkPng(track_id);
                if (png.has_value() && !png->empty()) {
                    std::ostringstream out;
                    out << "HTTP/1.1 200 OK\r\n"
                        << "Content-Type: image/png\r\n"
                        << "Cache-Control: public, max-age=86400\r\n"
                        << "Access-Control-Allow-Origin: *\r\n"
                        << "Content-Length: " << png->size() << "\r\n"
                        << "\r\n";
                    std::string response = out.str();
                    response.append(reinterpret_cast<const char*>(png->data()), png->size());
                    return response;
                }
            }
            return json::httpNotFound();
        }

        if (path == "/api/library/tracks") {
            std::ostringstream out;
            out << '[';
            const auto tracks = library_provider_->listTracks();
            for (std::size_t i = 0; i < tracks.size(); ++i) {
                if (i > 0) out << ',';
                const auto& t = tracks[i];
                out << '{';
                json::appendInt(out, "id", t.id);
                json::separator(out); json::appendString(out, "title", t.title);
                json::separator(out); json::appendString(out, "artist", t.artist);
                json::separator(out); json::appendString(out, "album", t.album);
                json::separator(out); json::appendString(out, "genre", t.genre);
                json::separator(out); json::appendInt(out, "duration_seconds", t.duration_seconds);
                json::separator(out); json::appendInt(out, "play_count", t.play_count);
                out << '}';
            }
            out << ']';
            std::string json = out.str();
            std::string response = json::httpOk("application/json; charset=utf-8", json.size());
            response += json;
            return response;
        }
        if (path == "/api/library/albums") {
            std::ostringstream out;
            out << '[';
            const auto albums = library_provider_->listAlbums();
            for (std::size_t i = 0; i < albums.size(); ++i) {
                if (i > 0) out << ',';
                const auto& a = albums[i];
                out << '{';
                json::appendString(out, "name", a.name);
                json::separator(out); json::appendString(out, "artist", a.artist);
                json::separator(out); json::appendInt(out, "track_count", a.track_count);
                if (!a.artwork_url.empty()) {
                    json::separator(out); json::appendString(out, "artwork_url", a.artwork_url);
                }
                if (a.first_track_id > 0) {
                    json::separator(out); json::appendInt(out, "first_track_id", a.first_track_id);
                }
                out << '}';
            }
            out << ']';
            std::string json = out.str();
            std::string response = json::httpOk("application/json; charset=utf-8", json.size());
            response += json;
            return response;
        }
    }

    // Enrichment endpoints delegated to the injected enrich provider.
    if (method == "GET" && enrich_provider_) {
        constexpr std::string_view kArtistPrefix = "/api/library/artist/";
        constexpr std::string_view kAlbumPrefix  = "/api/library/album/";

        if (path.starts_with(kArtistPrefix) && path.size() > kArtistPrefix.size()) {
            const auto encoded = path.substr(kArtistPrefix.size());
            const auto name = urlDecode(encoded);
            auto info = enrich_provider_->getArtistInfo(name);
            if (!info.has_value()) return json::httpNotFound();
            std::ostringstream out;
            out << '{';
            json::appendString(out, "artist", name);
            json::separator(out); json::appendString(out, "summary", info->summary);
            json::separator(out); json::appendString(out, "source", info->source);
            out << '}';
            std::string json = out.str();
            std::string response = json::httpOk("application/json; charset=utf-8", json.size());
            response += json;
            return response;
        }

        if (path.starts_with(kAlbumPrefix) && path.size() > kAlbumPrefix.size()) {
            const auto rest = path.substr(kAlbumPrefix.size());
            const auto sep = rest.find('/');
            if (sep != std::string_view::npos && sep + 1 < rest.size()) {
                const auto artist = urlDecode(rest.substr(0, sep));
                const auto album  = urlDecode(rest.substr(sep + 1));
                auto info = enrich_provider_->getAlbumInfo(album, artist);
                if (!info.has_value()) return json::httpNotFound();
                std::ostringstream out;
                out << '{';
                json::appendString(out, "album", album);
                json::separator(out); json::appendString(out, "artist", artist);
                json::separator(out); json::appendString(out, "summary", info->summary);
                json::separator(out); json::appendString(out, "source", info->source);
                json::separator(out); json::appendString(out, "genres", info->genres);
                json::separator(out); json::appendInt(out, "year", info->year);
                out << '}';
                std::string json = out.str();
                std::string response = json::httpOk("application/json; charset=utf-8", json.size());
                response += json;
                return response;
            }
        }
    }

    return json::httpNotFound();
}

bool WebUiHttpRouter::parseWebUiCommand(std::string_view json_body, runtime::RuntimeCommand& command)
{
    using runtime::RuntimeCommandKind;
    using runtime::RuntimeCommandPayload;

    // Extract the "kind" field
    std::string_view kind_raw = extractJsonString(json_body, "kind");
    if (kind_raw.empty()) return false;

    // Strip "webui." prefix
    std::string_view action = kind_raw;
    constexpr std::string_view prefix = "webui.";
    if (action.starts_with(prefix)) {
        action.remove_prefix(prefix.size());
    }

    // Extract origin and correlation_id
    std::string_view origin_raw = extractJsonString(json_body, "origin");
    command.origin = runtime::CommandOrigin::Automation; // WebUI commands come from automation

    std::string_view corr_id = extractJsonString(json_body, "correlation_id");
    if (!corr_id.empty()) {
        command.correlation_id = unescapeJson(corr_id);
    }

    // Try simple action mapping first
    if (mapActionToCommand(action, command)) {
        return true;
    }

    // Handle parameterised commands
    if (action == "QueueJump") {
        bool found = false;
        int queue_index = extractJsonInt(json_body, "queue_index", found);
        if (!found) return false;
        command.kind = RuntimeCommandKind::QueueJump;
        command.payload = RuntimeCommandPayload::queueIndex(queue_index);
        return true;
    }

    if (action == "PlayStartTrack") {
        bool found = false;
        int track_id = extractJsonInt(json_body, "track_id", found);
        if (!found) return false;
        command.kind = RuntimeCommandKind::PlaybackStartTrack;
        command.payload = RuntimeCommandPayload::startTrack(
            track_id,
            std::string{extractJsonString(json_body, "album")},
            std::string{extractJsonString(json_body, "artist")},
            std::string{extractJsonString(json_body, "genre")});
        return true;
    }

    if (action == "EqSetBand") {
        bool found_idx = false;
        bool found_db = false;
        int band_index = extractJsonInt(json_body, "eq_band_index", found_idx);
        int gain_db = extractJsonInt(json_body, "eq_gain_db", found_db);
        if (!found_idx || !found_db) return false;
        command.kind = RuntimeCommandKind::EqSetBand;
        command.payload = RuntimeCommandPayload::eqSetBand(band_index, gain_db);
        return true;
    }

    if (action == "EqCyclePreset") {
        bool found = false;
        int delta = extractJsonInt(json_body, "delta", found);
        if (!found) return false;
        command.kind = RuntimeCommandKind::EqCyclePreset;
        command.payload = RuntimeCommandPayload::eqCyclePreset(delta);
        return true;
    }

    if (action == "EqAdjustBand") {
        bool found_idx = false;
        bool found_delta = false;
        int band_index = extractJsonInt(json_body, "eq_band_index", found_idx);
        int gain_delta = extractJsonInt(json_body, "eq_gain_delta", found_delta);
        if (!found_idx || !found_delta) return false;
        command.kind = RuntimeCommandKind::EqAdjustBand;
        command.payload = RuntimeCommandPayload::eqAdjustBand(band_index, gain_delta);
        return true;
    }

    if (action == "Seek") {
        bool found = false;
        int seconds = extractJsonInt(json_body, "seconds", found);
        if (!found) return false;
        command.kind = RuntimeCommandKind::PlaybackSeek;
        command.payload = RuntimeCommandPayload::seek(static_cast<double>(seconds));
        return true;
    }

    return false;
}

} // namespace lofibox::webui
