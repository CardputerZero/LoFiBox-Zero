// SPDX-License-Identifier: GPL-3.0-or-later

#include <cassert>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "runtime/runtime_command.h"
#include "runtime/runtime_command_client.h"
#include "runtime/runtime_event.h"
#include "runtime/runtime_snapshot.h"
#include "webui/webui_config.h"
#include "webui/webui_http_router.h"
#include "webui/webui_json.h"
#include "webui/webui_projection.h"
#include "webui/webui_runtime_adapter.h"
#include "webui/webui_static_assets.h"

namespace {

// A minimal fake RuntimeCommandClient for testing the adapter.
class FakeRuntimeCommandClient : public lofibox::runtime::RuntimeCommandClient {
public:
    lofibox::runtime::RuntimeCommandResult dispatch(const lofibox::runtime::RuntimeCommand& command) override
    {
        last_command_ = command;
        lofibox::runtime::RuntimeCommandResult result{};
        result.accepted = true;
        result.applied = true;
        result.code = "ok";
        result.correlation_id = command.correlation_id;
        return result;
    }

    lofibox::runtime::RuntimeSnapshot query(const lofibox::runtime::RuntimeQuery& /*query*/) const override
    {
        lofibox::runtime::RuntimeSnapshot snap{};
        snap.playback.status = lofibox::runtime::RuntimePlaybackStatus::Playing;
        snap.playback.title = "Test Track";
        snap.playback.artist = "Test Artist";
        snap.playback.album = "Test Album";
        snap.playback.source_label = "Local";
        snap.playback.elapsed_seconds = 42.5;
        snap.playback.duration_seconds = 200;
        snap.playback.shuffle_enabled = true;
        snap.playback.volume_percent = 75;
        snap.eq.enabled = true;
        snap.eq.preset_name = "Rock";
        snap.eq.bands = {2, 1, 0, -1, 0, 2, 3, 1, 0, -2};
        snap.library.ready = true;
        snap.library.track_count = 1500;
        snap.library.album_count = 120;
        snap.library.artist_count = 80;
        snap.library.status = "READY";
        snap.diagnostics.runtime_ok = true;
        snap.diagnostics.audio_ok = true;
        snap.diagnostics.library_ok = true;
        snap.diagnostics.remote_ok = true;
        snap.diagnostics.cache_ok = true;
        snap.diagnostics.audio_backend = "ALSA";
        snap.diagnostics.output_device = "default";
        snap.diagnostics.library_track_count = 1500;
        snap.version = 42;
        return snap;
    }

    lofibox::runtime::RuntimeCommand last_command_{};
};

// --- Config Tests ---

void test_config_defaults()
{
    lofibox::webui::WebUiConfig config{};
    assert(!config.enabled);
    assert(config.bind_address == "0.0.0.0");
    assert(config.port == 8765);
    std::cout << "  PASS: config defaults\n";
}

void test_config_cli_parsing()
{
    lofibox::webui::WebUiConfig config{};

    // Token sequence: --webui
    {
        const char* args[] = {"lofibox", "--webui"};
        int consumed = lofibox::webui::parseWebUiCliArgs(2, const_cast<char**>(args), 1, config);
        assert(consumed == 1);
        assert(config.enabled);
    }

    // Token sequence: --webui-bind 0.0.0.0
    {
        lofibox::webui::WebUiConfig config2{};
        const char* args[] = {"lofibox", "--webui-bind", "0.0.0.0"};
        int consumed = lofibox::webui::parseWebUiCliArgs(3, const_cast<char**>(args), 1, config2);
        assert(consumed == 2);
        assert(!config2.enabled); // --webui-bind alone shouldn't enable
        assert(config2.bind_address == "0.0.0.0");
    }

    // Token sequence: --webui-port 9999
    {
        lofibox::webui::WebUiConfig config3{};
        const char* args[] = {"lofibox", "--webui-port", "9999"};
        int consumed = lofibox::webui::parseWebUiCliArgs(3, const_cast<char**>(args), 1, config3);
        assert(consumed == 2);
        assert(!config3.enabled);
        assert(config3.port == 9999);
    }

    // Invalid port should be ignored
    {
        lofibox::webui::WebUiConfig config4{};
        const char* args[] = {"lofibox", "--webui-port", "not-a-number"};
        int consumed = lofibox::webui::parseWebUiCliArgs(3, const_cast<char**>(args), 1, config4);
        assert(consumed == 2);
        assert(config4.port == 8765); // unchanged
    }

    std::cout << "  PASS: config CLI parsing\n";
}

void test_config_env_parsing()
{
    // NOTE: we can't reliably set env in a test that might run in parallel,
    // but we can verify parseWebUiEnv doesn't crash on missing env.
    lofibox::webui::WebUiConfig config{};
    lofibox::webui::parseWebUiEnv(config);
    // Defaults should remain if no env is set
    assert(!config.enabled);
    assert(config.bind_address == "0.0.0.0");
    assert(config.port == 8765);
    std::cout << "  PASS: config env parsing (no env set)\n";
}

// --- JSON Tests ---

void test_json_escape()
{
    using lofibox::webui::json::escape;
    assert(escape("hello") == "hello");
    assert(escape("a\"b") == "a\\\"b");
    assert(escape("a\\b") == "a\\\\b");
    assert(escape("a\nb") == "a\\nb");
    assert(escape("a\rb") == "a\\rb");
    assert(escape("a\tb") == "a\\tb");
    assert(escape("").empty());
    std::cout << "  PASS: json escape\n";
}

void test_json_helpers()
{
    using namespace lofibox::webui::json;

    {
        std::ostringstream out;
        out << '{';
        appendString(out, "key", "value");
        appendBool(out, "flag", true);
        appendInt(out, "num", 42);
        appendDouble(out, "pi", 3.14);
        out << '}';
        std::string result = out.str();
        assert(result.find("\"key\":\"value\"") != std::string::npos);
        assert(result.find("\"flag\":true") != std::string::npos);
        assert(result.find("\"num\":42") != std::string::npos);
        assert(result.find("\"pi\":3.14") != std::string::npos);
    }

    {
        std::ostringstream out;
        out << '{';
        openObject(out, "nested");
        appendString(out, "inner", "x");
        closeObject(out);
        out << '}';
        std::string result = out.str();
        assert(result.find("\"nested\":{") != std::string::npos);
        assert(result.find("\"inner\":\"x\"") != std::string::npos);
    }

    {
        std::ostringstream out;
        out << '{';
        appendStringArray(out, "tags", {"a", "b", "c"});
        out << '}';
        std::string result = out.str();
        assert(result.find("\"tags\":[\"a\",\"b\",\"c\"]") != std::string::npos);
    }

    std::cout << "  PASS: json helpers\n";
}

void test_json_http_responses()
{
    using namespace lofibox::webui::json;

    {
        std::string resp = httpOk("text/html", 1024);
        assert(resp.find("200 OK") != std::string::npos);
        assert(resp.find("Content-Type: text/html") != std::string::npos);
        assert(resp.find("Content-Length: 1024") != std::string::npos);
    }

    {
        std::string resp = httpNotFound();
        assert(resp.find("404 Not Found") != std::string::npos);
        assert(resp.find("Not Found") != std::string::npos);
    }

    {
        std::string resp = httpBadRequest("bad input");
        assert(resp.find("400 Bad Request") != std::string::npos);
        assert(resp.find("bad input") != std::string::npos);
    }

    {
        std::string resp = httpMethodNotAllowed();
        assert(resp.find("405 Method Not Allowed") != std::string::npos);
    }

    {
        std::string resp = httpServerError("boom");
        assert(resp.find("500 Internal Server Error") != std::string::npos);
        assert(resp.find("boom") != std::string::npos);
    }

    std::cout << "  PASS: http response builders\n";
}

// --- Projection Tests ---

void test_projection_empty_snapshot()
{
    lofibox::runtime::RuntimeSnapshot snap{};
    std::string json = lofibox::webui::buildFullSnapshotJson(snap);
    // Should produce valid JSON with the expected sections
    assert(!json.empty());
    assert(json.front() == '{');
    assert(json.find("\"playback\"") != std::string::npos);
    assert(json.find("\"queue\"") != std::string::npos);
    assert(json.find("\"eq\"") != std::string::npos);
    assert(json.find("\"library\"") != std::string::npos);
    assert(json.find("\"diagnostics\"") != std::string::npos);
    assert(json.find("\"plugins\"") != std::string::npos);
    assert(json.find("\"version\"") != std::string::npos);
    std::cout << "  PASS: projection empty snapshot\n";
}

void test_projection_now_playing()
{
    lofibox::runtime::RuntimeSnapshot snap{};
    snap.playback.status = lofibox::runtime::RuntimePlaybackStatus::Playing;
    snap.playback.title = "Bohemian Rhapsody";
    snap.playback.artist = "Queen";
    snap.playback.album = "A Night at the Opera";
    snap.playback.elapsed_seconds = 120.5;
    snap.playback.duration_seconds = 355;

    std::string json = lofibox::webui::buildNowPlayingJson(snap);
    assert(json.find("\"status\":\"playing\"") != std::string::npos);
    assert(json.find("\"title\":\"Bohemian Rhapsody\"") != std::string::npos);
    assert(json.find("\"artist\":\"Queen\"") != std::string::npos);
    assert(json.find("\"album\":\"A Night at the Opera\"") != std::string::npos);
    std::cout << "  PASS: projection now playing\n";
}

void test_projection_diagnostics()
{
    lofibox::runtime::RuntimeSnapshot snap{};
    snap.diagnostics.runtime_ok = true;
    snap.diagnostics.audio_ok = false;
    snap.diagnostics.warnings = {"low disk space"};
    snap.diagnostics.errors = {"audio device not found"};
    snap.plugins.selected_skin_id = "io.github.vicliu624.lofibox.theme.classic-dark";
    snap.plugins.loaded_plugin_ids = {"io.github.vicliu624.lofibox.theme.classic-dark"};
    snap.plugins.loaded_count = 1;

    std::string json = lofibox::webui::buildDiagnosticsJson(snap);
    assert(json.find("\"runtime_ok\":true") != std::string::npos);
    assert(json.find("\"audio_ok\":false") != std::string::npos);
    assert(json.find("\"warnings\":[\"low disk space\"]") != std::string::npos);
    assert(json.find("\"errors\":[\"audio device not found\"]") != std::string::npos);
    assert(json.find("\"plugins\"") != std::string::npos);
    assert(json.find("\"selected_skin_id\":\"io.github.vicliu624.lofibox.theme.classic-dark\"") != std::string::npos);
    std::cout << "  PASS: projection diagnostics\n";
}

void test_projection_eq()
{
    lofibox::runtime::RuntimeSnapshot snap{};
    snap.eq.enabled = true;
    snap.eq.preset_name = "Jazz";
    snap.eq.bands = {3, 2, 1, 0, -1, -2, -1, 0, 1, 2};

    std::string json = lofibox::webui::buildEqJson(snap);
    assert(json.find("\"enabled\":true") != std::string::npos);
    assert(json.find("\"preset_name\":\"Jazz\"") != std::string::npos);
    assert(json.find("\"bands\":[3,2,1,0,-1,-2,-1,0,1,2]") != std::string::npos);
    std::cout << "  PASS: projection eq\n";
}

void test_projection_event()
{
    lofibox::runtime::RuntimeEvent event{};
    event.kind = lofibox::runtime::RuntimeEventKind::PlaybackChanged;
    event.version = 100;
    event.elapsed_seconds = 30.0;
    event.duration_seconds = 240;
    event.snapshot.playback.title = "Event Track";
    event.snapshot.playback.artist = "Event Artist";
    event.snapshot.playback.status = lofibox::runtime::RuntimePlaybackStatus::Playing;
    event.current_index = 2;

    std::string json = lofibox::webui::buildEventJson(event);
    assert(json.find("\"event\":\"playback.changed\"") != std::string::npos);
    assert(json.find("\"title\":\"Event Track\"") != std::string::npos);
    assert(json.find("\"elapsed_seconds\":30,") != std::string::npos);
    assert(json.find("\"current_index\":2") != std::string::npos);

    // CRITICAL: buildEventJson writes playback fields directly inside
    // "snapshot" has no "playback" wrapper key. The frontend reads
    // msg.snapshot.status / msg.snapshot.current_track_id, NOT
    // msg.snapshot.playback.status.  A mismatch silently breaks all
    // event-driven UI updates.
    assert(json.find("\"snapshot\":{\"status\":\"playing\"") != std::string::npos);
    assert(json.find("\"snapshot\":{\"playback\"") == std::string::npos);
    assert(json.find("\"current_track_id\"") == std::string::npos); // not yet set in this event; covered by test below
    std::cout << "  PASS: projection event\n";
}

void test_projection_event_includes_current_track_id()
{
    lofibox::runtime::RuntimeEvent event{};
    event.kind = lofibox::runtime::RuntimeEventKind::PlaybackChanged;
    event.snapshot.playback.current_track_id = 42;
    event.snapshot.playback.status = lofibox::runtime::RuntimePlaybackStatus::Playing;
    event.snapshot.playback.title = "Current Track";

    std::string json = lofibox::webui::buildEventJson(event);
    assert(json.find("\"current_track_id\":42") != std::string::npos);
    assert(json.find("\"snapshot\":{\"status\":\"playing\"") != std::string::npos);
    assert(json.find("\"snapshot\":{\"playback\"") == std::string::npos);
    std::cout << "  PASS: projection event includes current_track_id\n";
}

// --- HTTP Router / Command Parsing Tests ---

void test_command_parse_toggle()
{
    FakeRuntimeCommandClient fake_client;
    lofibox::webui::WebUiRuntimeAdapter adapter{fake_client};
    lofibox::webui::WebUiHttpRouter router{adapter};

    lofibox::runtime::RuntimeCommand cmd{};
    bool ok = router.parseWebUiCommand(
        R"({"kind":"webui.Toggle","origin":"webui","correlation_id":"web-123"})",
        cmd);

    assert(ok);
    assert(cmd.kind == lofibox::runtime::RuntimeCommandKind::PlaybackToggle);
    assert(cmd.correlation_id == "web-123");
    std::cout << "  PASS: command parse Toggle\n";
}

void test_command_parse_previous()
{
    FakeRuntimeCommandClient fake_client;
    lofibox::webui::WebUiRuntimeAdapter adapter{fake_client};
    lofibox::webui::WebUiHttpRouter router{adapter};

    lofibox::runtime::RuntimeCommand cmd{};
    bool ok = router.parseWebUiCommand(
        R"({"kind":"webui.Previous","origin":"webui","correlation_id":"w-1"})",
        cmd);

    assert(ok);
    assert(cmd.kind == lofibox::runtime::RuntimeCommandKind::QueueStep);
    auto* step = cmd.payload.get<lofibox::runtime::QueueStepPayload>();
    assert(step != nullptr);
    assert(step->delta == -1);
    std::cout << "  PASS: command parse Previous (QueueStep -1)\n";
}

void test_command_parse_next()
{
    FakeRuntimeCommandClient fake_client;
    lofibox::webui::WebUiRuntimeAdapter adapter{fake_client};
    lofibox::webui::WebUiHttpRouter router{adapter};

    lofibox::runtime::RuntimeCommand cmd{};
    bool ok = router.parseWebUiCommand(
        R"({"kind":"webui.Next","origin":"webui","correlation_id":"w-2"})",
        cmd);

    assert(ok);
    assert(cmd.kind == lofibox::runtime::RuntimeCommandKind::QueueStep);
    auto* step = cmd.payload.get<lofibox::runtime::QueueStepPayload>();
    assert(step != nullptr);
    assert(step->delta == 1);
    std::cout << "  PASS: command parse Next (QueueStep +1)\n";
}

void test_command_parse_queue_jump()
{
    FakeRuntimeCommandClient fake_client;
    lofibox::webui::WebUiRuntimeAdapter adapter{fake_client};
    lofibox::webui::WebUiHttpRouter router{adapter};

    lofibox::runtime::RuntimeCommand cmd{};
    bool ok = router.parseWebUiCommand(
        R"({"kind":"webui.QueueJump","origin":"webui","correlation_id":"w-3","queue_index":5})",
        cmd);

    assert(ok);
    assert(cmd.kind == lofibox::runtime::RuntimeCommandKind::QueueJump);
    auto* idx = cmd.payload.get<lofibox::runtime::QueueIndexPayload>();
    assert(idx != nullptr);
    assert(idx->queue_index == 5);
    std::cout << "  PASS: command parse QueueJump\n";
}

void test_command_parse_eq_set_band()
{
    FakeRuntimeCommandClient fake_client;
    lofibox::webui::WebUiRuntimeAdapter adapter{fake_client};
    lofibox::webui::WebUiHttpRouter router{adapter};

    lofibox::runtime::RuntimeCommand cmd{};
    bool ok = router.parseWebUiCommand(
        R"({"kind":"webui.EqSetBand","origin":"webui","correlation_id":"w-4","eq_band_index":3,"eq_gain_db":5})",
        cmd);

    assert(ok);
    assert(cmd.kind == lofibox::runtime::RuntimeCommandKind::EqSetBand);
    auto* eq = cmd.payload.get<lofibox::runtime::EqSetBandPayload>();
    assert(eq != nullptr);
    assert(eq->eq_band_index == 3);
    assert(eq->eq_gain_db == 5);
    std::cout << "  PASS: command parse EqSetBand\n";
}

void test_command_parse_eq_cycle_preset()
{
    FakeRuntimeCommandClient fake_client;
    lofibox::webui::WebUiRuntimeAdapter adapter{fake_client};
    lofibox::webui::WebUiHttpRouter router{adapter};

    lofibox::runtime::RuntimeCommand cmd{};
    bool ok = router.parseWebUiCommand(
        R"({"kind":"webui.EqCyclePreset","origin":"webui","correlation_id":"w-5","delta":1})",
        cmd);

    assert(ok);
    assert(cmd.kind == lofibox::runtime::RuntimeCommandKind::EqCyclePreset);
    auto* preset = cmd.payload.get<lofibox::runtime::EqCyclePresetPayload>();
    assert(preset != nullptr);
    assert(preset->preset_delta == 1);
    std::cout << "  PASS: command parse EqCyclePreset\n";
}

void test_command_parse_invalid()
{
    FakeRuntimeCommandClient fake_client;
    lofibox::webui::WebUiRuntimeAdapter adapter{fake_client};
    lofibox::webui::WebUiHttpRouter router{adapter};

    lofibox::runtime::RuntimeCommand cmd{};
    bool ok = router.parseWebUiCommand(
        R"({"kind":"webui.UnknownAction","origin":"webui"})",
        cmd);

    assert(!ok); // Should fail for unknown action
    std::cout << "  PASS: command parse invalid action\n";
}

void test_command_parse_missing_params()
{
    FakeRuntimeCommandClient fake_client;
    lofibox::webui::WebUiRuntimeAdapter adapter{fake_client};
    lofibox::webui::WebUiHttpRouter router{adapter};

    // QueueJump without queue_index should fail
    lofibox::runtime::RuntimeCommand cmd{};
    bool ok = router.parseWebUiCommand(
        R"({"kind":"webui.QueueJump","origin":"webui"})",
        cmd);
    assert(!ok);

    // EqSetBand without gain should fail
    ok = router.parseWebUiCommand(
        R"({"kind":"webui.EqSetBand","eq_band_index":0})",
        cmd);
    assert(!ok);

    std::cout << "  PASS: command parse missing params\n";
}

// --- Static Assets Tests ---

void test_static_assets()
{
    const char* html = lofibox::webui::webUiIndexHtml();
    assert(html != nullptr);
    std::string_view html_sv{html};
    assert(html_sv.find("<!DOCTYPE html>") != std::string_view::npos);
    assert(html_sv.find("<title>LoFiBox Zero</title>") != std::string_view::npos);
    assert(html_sv.find("LoFiBox Zero") != std::string_view::npos);
    assert(html_sv.find("/api/theme.css") != std::string_view::npos);

    // CSS and JS are embedded in the HTML
    const char* css = lofibox::webui::webUiCss();
    assert(css != nullptr);

    const char* js = lofibox::webui::webUiJs();
    assert(js != nullptr);

    // MIME types
    assert(std::string_view{lofibox::webui::mimeTypeForPath("/index.html")}.find("text/html") != std::string_view::npos);
    assert(std::string_view{lofibox::webui::mimeTypeForPath("/style.css")}.find("text/css") != std::string_view::npos);
    assert(std::string_view{lofibox::webui::mimeTypeForPath("/app.js")}.find("javascript") != std::string_view::npos);
    assert(std::string_view{lofibox::webui::mimeTypeForPath("/icon.svg")}.find("image/svg+xml") != std::string_view::npos);

    // Asset lookup
    assert(lofibox::webui::assetContentForPath("/") != nullptr);
    assert(lofibox::webui::assetContentForPath("/index.html") != nullptr);
    assert(lofibox::webui::assetContentForPath("/nonexistent") == nullptr);

    std::cout << "  PASS: static assets\n";
}

// --- Runtime Adapter Tests ---

void test_runtime_adapter_basics()
{
    FakeRuntimeCommandClient fake_client;
    lofibox::webui::WebUiRuntimeAdapter adapter{fake_client};

    // Initial state
    assert(adapter.lastError().empty());
    assert(!adapter.isConnected());

    // Query snapshot
    auto snap = adapter.querySnapshot();
    assert(adapter.isConnected());
    assert(adapter.lastError().empty());
    assert(snap.version == 42);
    assert(snap.playback.title == "Test Track");

    // Last snapshot
    const auto& last = adapter.lastSnapshot();
    assert(last.playback.artist == "Test Artist");

    // Submit command
    lofibox::runtime::RuntimeCommand cmd{};
    cmd.kind = lofibox::runtime::RuntimeCommandKind::PlaybackToggle;
    cmd.correlation_id = "test-1";

    auto result = adapter.submitCommand(cmd);
    assert(result.accepted);
    assert(result.applied);

    // Prime the diff baseline; first pollEvents always detects changes
    // from the default-constructed previous snapshot.
    adapter.pollEvents();

    // Poll events again (should be empty since nothing changed)
    auto events = adapter.pollEvents();
    assert(events.empty());
    assert(adapter.isConnected());

    std::cout << "  PASS: runtime adapter basics\n";
}

// --- HTTP Router Response Tests ---

void test_http_router_responses()
{
    FakeRuntimeCommandClient fake_client;
    lofibox::webui::WebUiRuntimeAdapter adapter{fake_client};
    lofibox::webui::WebUiHttpRouter router{adapter};

    // GET /
    {
        std::string resp = router.handleRequest("GET", "/", "");
        assert(resp.find("200 OK") != std::string::npos);
        assert(resp.find("text/html") != std::string::npos);
        assert(resp.find("LoFiBox Zero") != std::string::npos);
    }

    // GET /index.html
    {
        std::string resp = router.handleRequest("GET", "/index.html", "");
        assert(resp.find("200 OK") != std::string::npos);
    }

    // GET /api/runtime/snapshot
    {
        adapter.querySnapshot(); // prime
        std::string resp = router.handleRequest("GET", "/api/runtime/snapshot", "");
        assert(resp.find("200 OK") != std::string::npos);
        assert(resp.find("application/json") != std::string::npos);
        assert(resp.find("\"title\":\"Test Track\"") != std::string::npos);
        assert(resp.find("\"plugins\"") != std::string::npos);
    }

    // GET /api/theme.css
    {
        std::string resp = router.handleRequest("GET", "/api/theme.css", "");
        assert(resp.find("200 OK") != std::string::npos);
        assert(resp.find("text/css") != std::string::npos);
        assert(resp.find("--lb-bg") != std::string::npos);
    }

    // POST /api/runtime/commands (valid)
    {
        std::string resp = router.handleRequest("POST", "/api/runtime/commands",
            R"({"kind":"webui.Toggle","origin":"webui","correlation_id":"test-1"})");
        assert(resp.find("200 OK") != std::string::npos);
        assert(resp.find("\"accepted\":true") != std::string::npos);
    }

    // POST /api/runtime/commands (invalid)
    {
        std::string resp = router.handleRequest("POST", "/api/runtime/commands",
            R"({"kind":"webui.BadAction"})");
        assert(resp.find("400 Bad Request") != std::string::npos);
    }

    // OPTIONS (CORS preflight)
    {
        std::string resp = router.handleRequest("OPTIONS", "/api/runtime/snapshot", "");
        assert(resp.find("204 No Content") != std::string::npos);
        assert(resp.find("Access-Control-Allow-Origin") != std::string::npos);
    }

    // Unknown path
    {
        std::string resp = router.handleRequest("GET", "/nonexistent", "");
        assert(resp.find("404 Not Found") != std::string::npos);
    }

    std::cout << "  PASS: http router responses\n";
}

} // namespace

int main()
{
    std::cout << "=== LoFiBox WebUI Smoke Tests ===\n\n";

    std::cout << "[Config]\n";
    test_config_defaults();
    test_config_cli_parsing();
    test_config_env_parsing();
    std::cout << '\n';

    std::cout << "[JSON Helpers]\n";
    test_json_escape();
    test_json_helpers();
    test_json_http_responses();
    std::cout << '\n';

    std::cout << "[Projection]\n";
    test_projection_empty_snapshot();
    test_projection_now_playing();
    test_projection_diagnostics();
    test_projection_eq();
    test_projection_event();
    test_projection_event_includes_current_track_id();
    std::cout << '\n';

    std::cout << "[Command Parsing]\n";
    test_command_parse_toggle();
    test_command_parse_previous();
    test_command_parse_next();
    test_command_parse_queue_jump();
    test_command_parse_eq_set_band();
    test_command_parse_eq_cycle_preset();
    test_command_parse_invalid();
    test_command_parse_missing_params();
    std::cout << '\n';

    std::cout << "[Static Assets]\n";
    test_static_assets();
    std::cout << '\n';

    std::cout << "[Runtime Adapter]\n";
    test_runtime_adapter_basics();
    std::cout << '\n';

    std::cout << "[HTTP Router]\n";
    test_http_router_responses();
    std::cout << '\n';

    std::cout << "All WebUI smoke tests passed.\n";
    return 0;
}
