// SPDX-License-Identifier: GPL-3.0-or-later

#include "cli/runtime_cli.h"

#include <array>
#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <initializer_list>
#include <optional>
#include <ostream>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "cli/cli_format.h"
#include "runtime/runtime_command.h"
#include "runtime/runtime_result.h"
#include "runtime/runtime_snapshot.h"
#include "runtime/unix_socket_runtime_transport.h"

namespace lofibox::cli {
namespace {

struct CliOption {
    std::string name{};
    std::string value{};
};

struct ParsedRuntimeArgs {
    std::vector<std::string_view> positional{};
    std::vector<CliOption> options{};
    std::optional<std::string> socket_path{};
};

bool isRuntimeCommand(std::string_view command) noexcept
{
    return command == "runtime"
        || command == "now"
        || command == "play"
        || command == "pause"
        || command == "resume"
        || command == "toggle"
        || command == "stop"
        || command == "seek"
        || command == "next"
        || command == "prev"
        || command == "queue"
        || command == "shuffle"
        || command == "repeat"
        || command == "eq"
        || command == "remix"
        || command == "remote";
}

ParsedRuntimeArgs parseArgs(int argc, char** argv)
{
    ParsedRuntimeArgs args{};
    for (int index = 1; index < argc; ++index) {
        const std::string_view current{argv[index]};
        if (current == "--runtime-socket" && (index + 1) < argc) {
            args.socket_path = argv[++index];
            continue;
        }
        if (current.rfind("--", 0) == 0) {
            const auto eq = current.find('=');
            if (eq != std::string_view::npos) {
                args.options.push_back({std::string{current.substr(2, eq - 2)}, std::string{current.substr(eq + 1)}});
            } else if ((index + 1) < argc && std::string_view{argv[index + 1]}.rfind("-", 0) != 0) {
                args.options.push_back({std::string{current.substr(2)}, argv[index + 1]});
                ++index;
            } else {
                args.options.push_back({std::string{current.substr(2)}, "true"});
            }
            continue;
        }
        args.positional.push_back(current);
    }
    return args;
}

std::optional<std::string> optionValue(const ParsedRuntimeArgs& args, std::string_view name)
{
    for (const auto& option : args.options) {
        if (option.name == name) {
            return option.value;
        }
    }
    return std::nullopt;
}

bool optionPresent(const ParsedRuntimeArgs& args, std::string_view name)
{
    return optionValue(args, name).has_value();
}

CliOutputOptions outputOptions(const ParsedRuntimeArgs& args)
{
    CliOutputOptions options{};
    options.json = optionPresent(args, "json");
    options.porcelain = optionPresent(args, "porcelain");
    options.quiet = optionPresent(args, "quiet");
    if (const auto fields = optionValue(args, "fields")) {
        options.fields = splitFields(*fields);
    }
    return options;
}

std::string correlation(std::string_view action)
{
    return "cli:" + std::string{action};
}

lofibox::runtime::RuntimeCommand command(lofibox::runtime::RuntimeCommandKind kind, lofibox::runtime::RuntimeCommandPayload payload = {})
{
    return lofibox::runtime::RuntimeCommand{kind, std::move(payload), lofibox::runtime::CommandOrigin::RuntimeCli, correlation("runtime")};
}

std::optional<int> parseInt(std::string_view value)
{
    try {
        return std::stoi(std::string{value});
    } catch (...) {
        return std::nullopt;
    }
}

std::optional<double> parseDouble(std::string_view value)
{
    try {
        return std::stod(std::string{value});
    } catch (...) {
        return std::nullopt;
    }
}

std::optional<double> parseDuration(std::string_view value)
{
    if (value.empty()) {
        return std::nullopt;
    }
    bool relative = false;
    if (value.front() == '+') {
        relative = true;
        value.remove_prefix(1);
    }
    if (!value.empty() && value.back() == 's') {
        value.remove_suffix(1);
    }
    const auto colon = value.find(':');
    if (colon != std::string_view::npos) {
        const auto minutes = parseDouble(value.substr(0, colon));
        const auto seconds = parseDouble(value.substr(colon + 1));
        if (!minutes || !seconds) {
            return std::nullopt;
        }
        return (*minutes * 60.0) + *seconds;
    }
    (void)relative;
    return parseDouble(value);
}

void printResult(const lofibox::runtime::RuntimeCommandResult& result, const CliOutputOptions& options, std::ostream& out)
{
    if (options.quiet) {
        return;
    }
    if (options.json) {
        out << '{';
        bool first = true;
        printJsonBoolField(out, "accepted", result.accepted, first);
        printJsonBoolField(out, "applied", result.applied, first);
        printJsonField(out, "code", result.code, first);
        printJsonField(out, "message", result.message, first);
        printJsonField(out, "correlation_id", result.correlation_id, first);
        printJsonNumberField(out, "version_before_apply", std::to_string(result.version_before_apply), first);
        printJsonNumberField(out, "version_after_apply", std::to_string(result.version_after_apply), first);
        out << "}\n";
        return;
    }
    if (!options.porcelain) {
        out << "Code: " << result.code << '\n'
            << "Applied: " << (result.applied ? "yes" : "no") << '\n'
            << "Message: " << result.message << '\n';
        return;
    }
    out << result.code << '\t' << (result.applied ? "APPLIED" : "NOT_APPLIED") << '\t' << result.message << '\n';
}

std::string playbackStatusLabel(lofibox::runtime::RuntimePlaybackStatus status)
{
    switch (status) {
    case lofibox::runtime::RuntimePlaybackStatus::Empty: return "EMPTY";
    case lofibox::runtime::RuntimePlaybackStatus::Paused: return "PAUSED";
    case lofibox::runtime::RuntimePlaybackStatus::Playing: return "PLAYING";
    }
    return "UNKNOWN";
}

std::string idsLabel(const std::vector<int>& ids)
{
    std::ostringstream out{};
    for (std::size_t index = 0; index < ids.size(); ++index) {
        if (index > 0U) {
            out << ',';
        }
        out << ids[index];
    }
    return out.str();
}

CliFields playbackFields(const lofibox::runtime::PlaybackRuntimeSnapshot& playback)
{
    return {
        {"status", playbackStatusLabel(playback.status)},
        {"track", playback.current_track_id ? std::to_string(*playback.current_track_id) : "-"},
        {"title", playback.title.empty() ? "-" : playback.title},
        {"artist", playback.artist.empty() ? "-" : playback.artist},
        {"album", playback.album.empty() ? "-" : playback.album},
        {"source", playback.source_label.empty() ? "-" : playback.source_label},
        {"source_type", playback.source_type.empty() ? "-" : playback.source_type},
        {"elapsed", std::to_string(playback.elapsed_seconds)},
        {"duration", std::to_string(playback.duration_seconds)},
        {"live", playback.live ? "YES" : "NO"},
        {"seekable", playback.seekable ? "YES" : "NO"},
        {"audio", playback.audio_active ? "ACTIVE" : "IDLE"},
        {"volume", std::to_string(playback.volume_percent)},
        {"codec", playback.codec.empty() ? "-" : playback.codec},
        {"bitrate", std::to_string(playback.bitrate_kbps)},
        {"shuffle", playback.shuffle_enabled ? "ON" : "OFF"},
        {"repeat", playback.repeat_one ? "ONE" : (playback.repeat_all ? "ALL" : "OFF")},
        {"version", std::to_string(playback.version)},
    };
}

CliFields queueFields(const lofibox::runtime::QueueRuntimeSnapshot& queue)
{
    return {
        {"index", std::to_string(queue.active_index)},
        {"tracks", idsLabel(queue.active_ids)},
        {"visible", std::to_string(queue.visible_items.size())},
        {"shuffle", queue.shuffle_enabled ? "ON" : "OFF"},
        {"repeat", queue.repeat_one ? "ONE" : (queue.repeat_all ? "ALL" : "OFF")},
        {"version", std::to_string(queue.version)},
    };
}

std::string bandsLabel(const lofibox::runtime::EqRuntimeSnapshot& eq)
{
    std::ostringstream out{};
    bool first = true;
    for (const int gain : eq.bands) {
        if (!first) out << ',';
        first = false;
        out << gain;
    }
    return out.str();
}

CliFields eqFields(const lofibox::runtime::EqRuntimeSnapshot& eq)
{
    return {
        {"enabled", eq.enabled ? "ON" : "OFF"},
        {"preset", eq.preset_name},
        {"effect", eq.effect_name},
        {"effect_id", eq.effect_id.empty() ? "-" : eq.effect_id},
        {"bands", bandsLabel(eq)},
        {"version", std::to_string(eq.version)},
    };
}

CliFields remoteFields(const lofibox::runtime::RemoteSessionSnapshot& remote)
{
    return {
        {"profile", remote.profile_id.empty() ? "-" : remote.profile_id},
        {"source", remote.source_label},
        {"connection", remote.connection_status},
        {"stream", remote.stream_resolved ? "RESOLVED" : "EMPTY"},
        {"url", remote.redacted_url.empty() ? "-" : remote.redacted_url},
        {"buffer", remote.buffer_state},
        {"recovery", remote.recovery_action},
        {"bitrate", std::to_string(remote.bitrate_kbps)},
        {"codec", remote.codec.empty() ? "-" : remote.codec},
        {"live", remote.live ? "YES" : "NO"},
        {"seekable", remote.seekable ? "YES" : "NO"},
        {"version", std::to_string(remote.version)},
    };
}

CliFields settingsFields(const lofibox::runtime::SettingsRuntimeSnapshot& settings)
{
    return {
        {"output", settings.output_mode},
        {"network", settings.network_policy},
        {"sleep", settings.sleep_timer},
        {"version", std::to_string(settings.version)},
    };
}

std::string floatBandsLabel(const std::array<float, 10>& bands)
{
    std::ostringstream out{};
    bool first = true;
    for (const float value : bands) {
        if (!first) out << ',';
        first = false;
        out << value;
    }
    return out.str();
}

CliFields visualizationFields(const lofibox::runtime::VisualizationRuntimeSnapshot& visualization)
{
    return {
        {"available", visualization.available ? "YES" : "NO"},
        {"bands", floatBandsLabel(visualization.bands)},
        {"mode", visualization.mode},
        {"frame", std::to_string(visualization.frame_index)},
        {"version", std::to_string(visualization.version)},
    };
}

CliFields lyricsFields(const lofibox::runtime::LyricsRuntimeSnapshot& lyrics)
{
    return {
        {"available", lyrics.available ? "YES" : "NO"},
        {"synced", lyrics.synced ? "YES" : "NO"},
        {"source", lyrics.source.empty() ? "-" : lyrics.source},
        {"current", std::to_string(lyrics.current_index)},
        {"lines", std::to_string(lyrics.visible_lines.size())},
        {"status", lyrics.status_message},
        {"version", std::to_string(lyrics.version)},
    };
}

CliFields libraryFields(const lofibox::runtime::LibraryRuntimeSnapshot& library)
{
    return {
        {"ready", library.ready ? "YES" : "NO"},
        {"degraded", library.degraded ? "YES" : "NO"},
        {"tracks", std::to_string(library.track_count)},
        {"albums", std::to_string(library.album_count)},
        {"artists", std::to_string(library.artist_count)},
        {"status", library.status},
        {"version", std::to_string(library.version)},
    };
}

CliFields diagnosticsFields(const lofibox::runtime::DiagnosticsRuntimeSnapshot& diagnostics)
{
    return {
        {"runtime", diagnostics.runtime_ok ? "OK" : "FAIL"},
        {"audio", diagnostics.audio_ok ? "OK" : "FAIL"},
        {"library", diagnostics.library_ok ? "OK" : "FAIL"},
        {"remote", diagnostics.remote_ok ? "OK" : "FAIL"},
        {"cache", diagnostics.cache_ok ? "OK" : "FAIL"},
        {"backend", diagnostics.audio_backend.empty() ? "-" : diagnostics.audio_backend},
        {"warnings", std::to_string(diagnostics.warnings.size())},
        {"errors", std::to_string(diagnostics.errors.size())},
        {"version", std::to_string(diagnostics.version)},
    };
}

CliFields creatorFields(const lofibox::runtime::CreatorRuntimeSnapshot& creator)
{
    return {
        {"available", creator.available ? "YES" : "NO"},
        {"bpm", std::to_string(creator.bpm)},
        {"key", creator.key.empty() ? "-" : creator.key},
        {"lufs", std::to_string(creator.lufs)},
        {"dynamic_range", std::to_string(creator.dynamic_range)},
        {"waveform_points", std::to_string(creator.waveform_points.size())},
        {"beats", std::to_string(creator.beat_grid_seconds.size())},
        {"stems", creator.stem_status},
        {"source", creator.analysis_source.empty() ? "-" : creator.analysis_source},
        {"confidence", creator.confidence.empty() ? "-" : creator.confidence},
        {"status", creator.status_message},
        {"version", std::to_string(creator.version)},
    };
}

std::vector<std::string> fieldNames(const CliFields& fields)
{
    std::vector<std::string> names{};
    names.reserve(fields.size());
    for (const auto& [name, value] : fields) {
        (void)value;
        names.push_back(name);
    }
    return names;
}

bool validateObjectFields(
    const CliOutputOptions& options,
    const CliFields& fields,
    std::string_view command_name,
    std::ostream& err)
{
    if (options.fields.empty()) {
        return true;
    }
    const auto allowed = fieldNames(fields);
    for (const auto& field : options.fields) {
        if (!containsField(allowed, field)) {
            printCliError(err, options, CliStructuredError{
                "INVALID_FIELD",
                "Unknown field for " + std::string{command_name} + ": " + field,
                "--fields",
                "one of the allowed fields",
                static_cast<int>(CliExitCode::Usage),
                "lofibox " + std::string{command_name} + " --fields " + joinFields(allowed),
                allowed});
            return false;
        }
    }
    return true;
}

std::vector<std::string> fullSnapshotAllowedFields(const lofibox::runtime::RuntimeSnapshot& snapshot)
{
    std::vector<std::string> allowed{"version"};
    const auto add = [&allowed](std::string_view domain, const CliFields& fields) {
        for (const auto& [name, value] : fields) {
            (void)value;
            allowed.push_back(std::string{domain} + "." + name);
        }
    };
    add("playback", playbackFields(snapshot.playback));
    add("queue", queueFields(snapshot.queue));
    add("eq", eqFields(snapshot.eq));
    add("remote", remoteFields(snapshot.remote));
    add("settings", settingsFields(snapshot.settings));
    add("visualization", visualizationFields(snapshot.visualization));
    add("lyrics", lyricsFields(snapshot.lyrics));
    add("library", libraryFields(snapshot.library));
    add("diagnostics", diagnosticsFields(snapshot.diagnostics));
    add("creator", creatorFields(snapshot.creator));
    return allowed;
}

bool playbackBareFieldAllowed(const lofibox::runtime::RuntimeSnapshot& snapshot, std::string_view name)
{
    return containsField(fieldNames(playbackFields(snapshot.playback)), name);
}

bool validateFullFields(
    const CliOutputOptions& options,
    const lofibox::runtime::RuntimeSnapshot& snapshot,
    std::ostream& err)
{
    if (options.fields.empty()) {
        return true;
    }
    const auto allowed = fullSnapshotAllowedFields(snapshot);
    for (const auto& field : options.fields) {
        if (containsField(allowed, field) || playbackBareFieldAllowed(snapshot, field)) {
            continue;
        }
        printCliError(err, options, CliStructuredError{
            "INVALID_FIELD",
            "Unknown runtime snapshot field: " + field,
            "--fields",
            "nested field such as playback.status or queue.index",
            static_cast<int>(CliExitCode::Usage),
            "lofibox now --fields playback.status,playback.title --json",
            allowed});
        return false;
    }
    return true;
}

bool wantsFullField(
    const CliOutputOptions& options,
    std::string_view domain,
    std::string_view field,
    const lofibox::runtime::RuntimeSnapshot& snapshot)
{
    if (options.fields.empty()) {
        return true;
    }
    const auto nested = std::string{domain} + "." + std::string{field};
    if (containsField(options.fields, nested)) {
        return true;
    }
    return domain == "playback" && playbackBareFieldAllowed(snapshot, field) && containsField(options.fields, field);
}

bool wantsFullDomain(
    const CliOutputOptions& options,
    std::string_view domain,
    const CliFields& fields,
    const lofibox::runtime::RuntimeSnapshot& snapshot)
{
    if (options.fields.empty()) {
        return true;
    }
    for (const auto& [name, value] : fields) {
        (void)value;
        if (wantsFullField(options, domain, name, snapshot)) {
            return true;
        }
    }
    return false;
}

void printFullJsonDomain(
    std::ostream& out,
    std::string_view domain,
    const CliFields& fields,
    const CliOutputOptions& options,
    const lofibox::runtime::RuntimeSnapshot& snapshot,
    bool& first_top)
{
    if (!wantsFullDomain(options, domain, fields, snapshot)) {
        return;
    }
    if (!first_top) {
        out << ',';
    }
    first_top = false;
    printJsonString(out, domain);
    out << ":{";
    bool first_field = true;
    for (const auto& [name, value] : fields) {
        if (wantsFullField(options, domain, name, snapshot)) {
            printJsonField(out, name, value, first_field);
        }
    }
    out << '}';
}

bool hasHelpRequest(const ParsedRuntimeArgs& args)
{
    if (optionPresent(args, "help")) {
        return true;
    }
    for (const auto item : args.positional) {
        if (item == "-h" || item == "help") {
            return true;
        }
    }
    return false;
}

bool hasSchemaRequest(const ParsedRuntimeArgs& args)
{
    return optionPresent(args, "schema");
}

std::string commandPath(const ParsedRuntimeArgs& args)
{
    std::ostringstream out{};
    bool first = true;
    for (const auto item : args.positional) {
        if (item == "help" || item == "-h") {
            continue;
        }
        if (!first) {
            out << ' ';
        }
        first = false;
        out << item;
    }
    return out.str();
}

void printRuntimeHelp(const ParsedRuntimeArgs& args, std::ostream& out)
{
    const auto path = commandPath(args);
    if (path == "runtime playback" || path == "runtime now" || path == "now") {
        out << "Usage: lofibox runtime playback [--json] [--fields status,title,...]\n"
            << "       lofibox now [--json] [--fields playback.status,queue.index,...]\n"
            << "Query live playback or full runtime status through the running runtime socket.\n"
            << "Fields: status,track,title,artist,album,source,source_type,elapsed,duration,live,seekable,audio,volume,codec,bitrate,shuffle,repeat,version\n";
        return;
    }
    if (path == "play") {
        out << "Usage: lofibox play [<path-or-url>|--id <track-id>|--source <profile-id> --item <remote-item-id>|--pause|--resume|--toggle|--next|--prev|--stop|--seek <seconds|MM:SS>|--shuffle on|off|--repeat off|one|all]\n"
            << "Mutates the running playback session. --shuffle on/off is idempotent.\n";
        return;
    }
    if (path == "queue" || path == "runtime queue") {
        out << "Usage: lofibox queue show|set <index>|jump <index>|next|clear [--json] [--fields index,tracks,...]\n"
            << "Mutates or queries the running active queue through the runtime command bus.\n"
            << "Fields: index,tracks,visible,shuffle,repeat,version\n";
        return;
    }
    if (path == "eq" || path == "runtime eq") {
        out << "Usage: lofibox eq show|enable|disable|preset <name>|band <index> <gain>|reset [--json]\n"
            << "Queries or mutates the live DSP/EQ runtime state.\n"
            << "Fields: enabled,preset,effect,effect_id,bands,version\n";
        return;
    }
    if (path == "remix") {
        out << "Usage: lofibox remix [--json]\n"
            << "Cycles the built-in Radio/Tape/Vinyl realtime remix effect.\n";
        return;
    }
    if (path == "remote" || path == "runtime remote") {
        out << "Usage: lofibox remote reconnect\n"
            << "       lofibox runtime remote [--json] [--fields profile,source,connection,...]\n"
            << "Queries or reconnects the active remote runtime session.\n";
        return;
    }
    out << "Runtime LoFiBox commands:\n"
        << "  now [--json] [--fields playback.status,queue.index,...]\n"
        << "  runtime status|playback|queue|eq|remote|settings [--json] [--fields ...]\n"
        << "  play [<path-or-url>|--id <track-id>|--source <profile-id> --item <remote-item-id>|--pause|--resume|--toggle|--next|--prev|--stop|--seek <seconds>|--shuffle on|off|--repeat off|one|all]\n"
        << "  pause|resume|toggle|stop|seek <seconds>|next|prev\n"
        << "  queue show|set <index>|jump <index>|next|clear\n"
        << "  shuffle on|off\n"
        << "  repeat off|all|one\n"
        << "  eq show|enable|disable|preset <name>|band <index> <gain>|reset\n"
        << "  remote reconnect\n"
        << "  runtime reload|runtime shutdown\n";
}

void printJsonStringArray(std::ostream& out, std::string_view name, const std::vector<std::string>& values, bool& first)
{
    if (!first) {
        out << ',';
    }
    first = false;
    printJsonString(out, name);
    out << ":[";
    for (std::size_t index = 0; index < values.size(); ++index) {
        if (index > 0U) {
            out << ',';
        }
        printJsonString(out, values[index]);
    }
    out << ']';
}

void printJsonStringArray(std::ostream& out, std::string_view name, std::initializer_list<std::string_view> values, bool& first)
{
    if (!first) {
        out << ',';
    }
    first = false;
    printJsonString(out, name);
    out << ":[";
    bool first_value = true;
    for (const auto value : values) {
        if (!first_value) {
            out << ',';
        }
        first_value = false;
        printJsonString(out, value);
    }
    out << ']';
}

void printRuntimeSchema(const ParsedRuntimeArgs& args, std::ostream& out)
{
    const auto path = commandPath(args);
    const bool query = path == "now"
        || path == "runtime"
        || path == "runtime status"
        || path == "runtime playback"
        || path == "runtime queue"
        || path == "runtime eq"
        || path == "runtime remote"
        || path == "runtime settings"
        || path == "queue"
        || path == "queue show"
        || path == "eq"
        || path == "eq show";
    const bool mutates = !query;
    const std::vector<std::string> fields = path == "runtime playback" ? fieldNames(playbackFields({}))
        : (path == "runtime queue" || path == "queue" || path == "queue show") ? fieldNames(queueFields({}))
        : (path == "runtime eq" || path == "eq" || path == "eq show") ? fieldNames(eqFields({}))
        : path == "runtime remote" ? fieldNames(remoteFields({}))
        : path == "runtime settings" ? fieldNames(settingsFields({}))
        : std::vector<std::string>{
            "version",
            "playback.status",
            "playback.title",
            "playback.artist",
            "playback.elapsed",
            "playback.duration",
            "queue.index",
            "queue.visible",
            "eq.enabled",
            "remote.connection",
            "lyrics.available",
            "visualization.available"};

    out << '{';
    bool first = true;
    printJsonField(out, "command", path.empty() ? "runtime" : path, first);
    printJsonField(out, "kind", query ? "query" : "command", first);
    printJsonBoolField(out, "requires_runtime", true, first);
    printJsonBoolField(out, "mutates", mutates, first);
    printJsonStringArray(out, "fields", fields, first);
    printJsonStringArray(out, "global_options", {"--json", "--porcelain", "--fields", "--quiet", "--runtime-socket"}, first);
    if (path == "play") {
        printJsonStringArray(out, "options", {"--id <track-id>", "--source <profile-id>", "--item <remote-item-id>", "--pause", "--resume", "--toggle", "--next", "--prev", "--stop", "--seek <seconds|MM:SS>", "--shuffle on|off", "--repeat off|one|all"}, first);
    }
    if (!first) out << ',';
    first = false;
    printJsonString(out, "exit_codes");
    out << ":{\"0\":\"success\",\"2\":\"usage or invalid argument\",\"5\":\"runtime transport unavailable\",\"6\":\"playback backend failure\"}";
    out << "}\n";
}

void printPlayback(const lofibox::runtime::PlaybackRuntimeSnapshot& playback, const CliOutputOptions& options, std::ostream& out)
{
    printObject(out, options, playbackFields(playback));
}

void printQueue(const lofibox::runtime::QueueRuntimeSnapshot& queue, const CliOutputOptions& options, std::ostream& out)
{
    printObject(out, options, queueFields(queue));
}

void printEq(const lofibox::runtime::EqRuntimeSnapshot& eq, const CliOutputOptions& options, std::ostream& out)
{
    printObject(out, options, eqFields(eq));
}

void printRemote(const lofibox::runtime::RemoteSessionSnapshot& remote, const CliOutputOptions& options, std::ostream& out)
{
    printObject(out, options, remoteFields(remote));
}

void printSettings(const lofibox::runtime::SettingsRuntimeSnapshot& settings, const CliOutputOptions& options, std::ostream& out)
{
    printObject(out, options, settingsFields(settings));
}

void printFull(const lofibox::runtime::RuntimeSnapshot& snapshot, const CliOutputOptions& options, std::ostream& out)
{
    if (options.quiet) {
        return;
    }
    if (!options.json) {
        CliFields fields{};
        if (options.fields.empty() || containsField(options.fields, "version")) {
            fields.push_back({"version", std::to_string(snapshot.version)});
        }
        const auto add = [&fields, &options, &snapshot](std::string_view domain, const CliFields& domain_fields) {
            for (const auto& [name, value] : domain_fields) {
                if (!wantsFullField(options, domain, name, snapshot)) {
                    continue;
                }
                fields.push_back({std::string{domain} + "." + name, value});
            }
        };
        add("playback", playbackFields(snapshot.playback));
        if (!options.fields.empty()) {
            add("queue", queueFields(snapshot.queue));
            add("eq", eqFields(snapshot.eq));
            add("remote", remoteFields(snapshot.remote));
            add("settings", settingsFields(snapshot.settings));
            add("visualization", visualizationFields(snapshot.visualization));
            add("lyrics", lyricsFields(snapshot.lyrics));
            add("library", libraryFields(snapshot.library));
            add("diagnostics", diagnosticsFields(snapshot.diagnostics));
            add("creator", creatorFields(snapshot.creator));
        }
        auto unfiltered = options;
        unfiltered.fields.clear();
        printObject(out, unfiltered, fields);
        return;
    }
    out << '{';
    bool first_top = true;
    if (options.fields.empty() || containsField(options.fields, "version")) {
        printJsonNumberField(out, "version", std::to_string(snapshot.version), first_top);
    }
    printFullJsonDomain(out, "playback", playbackFields(snapshot.playback), options, snapshot, first_top);
    printFullJsonDomain(out, "queue", queueFields(snapshot.queue), options, snapshot, first_top);
    printFullJsonDomain(out, "eq", eqFields(snapshot.eq), options, snapshot, first_top);
    printFullJsonDomain(out, "remote", remoteFields(snapshot.remote), options, snapshot, first_top);
    printFullJsonDomain(out, "settings", settingsFields(snapshot.settings), options, snapshot, first_top);
    printFullJsonDomain(out, "visualization", visualizationFields(snapshot.visualization), options, snapshot, first_top);
    printFullJsonDomain(out, "lyrics", lyricsFields(snapshot.lyrics), options, snapshot, first_top);
    printFullJsonDomain(out, "library", libraryFields(snapshot.library), options, snapshot, first_top);
    printFullJsonDomain(out, "diagnostics", diagnosticsFields(snapshot.diagnostics), options, snapshot, first_top);
    printFullJsonDomain(out, "creator", creatorFields(snapshot.creator), options, snapshot, first_top);
    out << "}\n";
}

CliStructuredError invalidArgument(
    std::string message,
    std::string argument,
    std::string expected,
    std::string usage)
{
    return CliStructuredError{
        "INVALID_ARGUMENT",
        std::move(message),
        std::move(argument),
        std::move(expected),
        static_cast<int>(CliExitCode::Usage),
        std::move(usage),
        {}};
}

std::optional<lofibox::runtime::RuntimeCommand> buildCommand(
    const ParsedRuntimeArgs& args,
    std::optional<CliStructuredError>& parse_error)
{
    const auto& p = args.positional;
    if (p.empty()) {
        return std::nullopt;
    }
    const auto first = p[0];
    if (first == "play") {
        if (optionPresent(args, "pause")) return command(lofibox::runtime::RuntimeCommandKind::PlaybackPause);
        if (optionPresent(args, "resume")) return command(lofibox::runtime::RuntimeCommandKind::PlaybackResume);
        if (optionPresent(args, "toggle")) return command(lofibox::runtime::RuntimeCommandKind::PlaybackToggle);
        if (optionPresent(args, "next")) return command(lofibox::runtime::RuntimeCommandKind::QueueStep, lofibox::runtime::RuntimeCommandPayload::queueStep(1));
        if (optionPresent(args, "prev")) return command(lofibox::runtime::RuntimeCommandKind::QueueStep, lofibox::runtime::RuntimeCommandPayload::queueStep(-1));
        if (optionPresent(args, "stop")) return command(lofibox::runtime::RuntimeCommandKind::PlaybackStop);
        const auto source = optionValue(args, "source");
        const auto item = optionValue(args, "item");
        if (source || item) {
            if (!source || source->empty()) {
                parse_error = invalidArgument(
                    "play --source/--item requires a remote source profile id.",
                    "--source",
                    "remote source profile id",
                    "lofibox play --source <profile-id> --item <remote-item-id>");
                return std::nullopt;
            }
            if (!item || item->empty()) {
                parse_error = invalidArgument(
                    "play --source/--item requires a remote item id.",
                    "--item",
                    "remote item id",
                    "lofibox play --source <profile-id> --item <remote-item-id>");
                return std::nullopt;
            }
            return command(
                lofibox::runtime::RuntimeCommandKind::RemoteResolveAndStartTrack,
                lofibox::runtime::RuntimeCommandPayload::remoteTrackRef(*source, *item));
        }
        if (const auto id = optionValue(args, "id")) {
            const auto track_id = parseInt(*id);
            if (!track_id) {
                parse_error = invalidArgument(
                    "play --id requires a numeric track id.",
                    "--id",
                    "integer track id",
                    "lofibox play --id <track-id>");
                return std::nullopt;
            }
            return command(lofibox::runtime::RuntimeCommandKind::PlaybackStartTrack, lofibox::runtime::RuntimeCommandPayload::startTrack(*track_id));
        }
        if (const auto seek = optionValue(args, "seek")) {
            const auto seconds = parseDuration(*seek);
            if (!seconds) {
                parse_error = invalidArgument(
                    "play --seek requires seconds or MM:SS.",
                    "--seek",
                    "seconds, +seconds, or MM:SS",
                    "lofibox play --seek <seconds|MM:SS>");
                return std::nullopt;
            }
            return command(lofibox::runtime::RuntimeCommandKind::PlaybackSeek, lofibox::runtime::RuntimeCommandPayload::seek(*seconds));
        }
        if (const auto shuffle = optionValue(args, "shuffle")) {
            if (*shuffle == "on") {
                return command(lofibox::runtime::RuntimeCommandKind::PlaybackSetShuffle, lofibox::runtime::RuntimeCommandPayload::enabled(true));
            }
            if (*shuffle == "off") {
                return command(lofibox::runtime::RuntimeCommandKind::PlaybackSetShuffle, lofibox::runtime::RuntimeCommandPayload::enabled(false));
            }
            parse_error = invalidArgument(
                "play --shuffle requires on or off.",
                "--shuffle",
                "on|off",
                "lofibox play --shuffle on|off");
            return std::nullopt;
        }
        if (const auto repeat = optionValue(args, "repeat")) {
            if (*repeat == "all") return command(lofibox::runtime::RuntimeCommandKind::PlaybackSetRepeatAll, lofibox::runtime::RuntimeCommandPayload::enabled(true));
            if (*repeat == "one") return command(lofibox::runtime::RuntimeCommandKind::PlaybackSetRepeatOne, lofibox::runtime::RuntimeCommandPayload::enabled(true));
            if (*repeat == "off") return command(lofibox::runtime::RuntimeCommandKind::PlaybackSetRepeatAll, lofibox::runtime::RuntimeCommandPayload::enabled(false));
        }
        return command(lofibox::runtime::RuntimeCommandKind::PlaybackPlay);
    }
    if (first == "pause") return command(lofibox::runtime::RuntimeCommandKind::PlaybackPause);
    if (first == "resume") return command(lofibox::runtime::RuntimeCommandKind::PlaybackResume);
    if (first == "toggle") return command(lofibox::runtime::RuntimeCommandKind::PlaybackToggle);
    if (first == "stop") return command(lofibox::runtime::RuntimeCommandKind::PlaybackStop);
    if (first == "next") return command(lofibox::runtime::RuntimeCommandKind::QueueStep, lofibox::runtime::RuntimeCommandPayload::queueStep(1));
    if (first == "prev") return command(lofibox::runtime::RuntimeCommandKind::QueueStep, lofibox::runtime::RuntimeCommandPayload::queueStep(-1));
    if (first == "seek" && p.size() >= 2) {
        const auto seconds = parseDuration(p[1]);
        if (!seconds) {
            parse_error = invalidArgument(
                "seek requires seconds or MM:SS.",
                "seek",
                "seconds, +seconds, or MM:SS",
                "lofibox seek <seconds|MM:SS>");
            return std::nullopt;
        }
        return command(lofibox::runtime::RuntimeCommandKind::PlaybackSeek, lofibox::runtime::RuntimeCommandPayload::seek(*seconds));
    }
    if (first == "queue" && p.size() >= 2) {
        if (p[1] == "next") {
            return command(lofibox::runtime::RuntimeCommandKind::QueueStep, lofibox::runtime::RuntimeCommandPayload::queueStep(1));
        }
        if (p[1] == "set" && p.size() >= 3) {
            const auto index = parseInt(p[2]);
            if (!index) {
                parse_error = invalidArgument(
                    "queue set requires numeric index.",
                    "index",
                    "integer queue index",
                    "lofibox queue set <index>");
                return std::nullopt;
            }
            return command(lofibox::runtime::RuntimeCommandKind::QueueJump, lofibox::runtime::RuntimeCommandPayload::queueIndex(*index));
        }
        if (p[1] == "jump" && p.size() >= 3) {
            const auto index = parseInt(p[2]);
            if (!index) {
                parse_error = invalidArgument(
                    "queue jump requires numeric index.",
                    "index",
                    "integer queue index",
                    "lofibox queue jump <index>");
                return std::nullopt;
            }
            return command(lofibox::runtime::RuntimeCommandKind::QueueJump, lofibox::runtime::RuntimeCommandPayload::queueIndex(*index));
        }
        if (p[1] == "clear") {
            return command(lofibox::runtime::RuntimeCommandKind::QueueClear);
        }
    }
    if (first == "shuffle" && p.size() >= 2) {
        if (p[1] == "on") return command(lofibox::runtime::RuntimeCommandKind::PlaybackSetShuffle, lofibox::runtime::RuntimeCommandPayload::enabled(true));
        if (p[1] == "off") return command(lofibox::runtime::RuntimeCommandKind::PlaybackSetShuffle, lofibox::runtime::RuntimeCommandPayload::enabled(false));
        parse_error = invalidArgument(
            "shuffle requires on or off.",
            "shuffle",
            "on|off",
            "lofibox shuffle on|off");
        return std::nullopt;
    }
    if (first == "repeat" && p.size() >= 2) {
        if (p[1] == "all") return command(lofibox::runtime::RuntimeCommandKind::PlaybackSetRepeatAll, lofibox::runtime::RuntimeCommandPayload::enabled(true));
        if (p[1] == "one") return command(lofibox::runtime::RuntimeCommandKind::PlaybackSetRepeatOne, lofibox::runtime::RuntimeCommandPayload::enabled(true));
        if (p[1] == "off") return command(lofibox::runtime::RuntimeCommandKind::PlaybackSetRepeatAll, lofibox::runtime::RuntimeCommandPayload::enabled(false));
    }
    if (first == "eq" && p.size() >= 2) {
        if (p[1] == "enable") return command(lofibox::runtime::RuntimeCommandKind::EqEnable);
        if (p[1] == "disable") return command(lofibox::runtime::RuntimeCommandKind::EqDisable);
        if (p[1] == "reset") return command(lofibox::runtime::RuntimeCommandKind::EqReset);
        if (p[1] == "preset" && p.size() >= 3) return command(lofibox::runtime::RuntimeCommandKind::EqApplyPreset, lofibox::runtime::RuntimeCommandPayload::eqApplyPreset(std::string{p[2]}));
        if (p[1] == "band" && p.size() >= 4) {
            const auto band = parseInt(p[2]);
            const auto gain = parseInt(p[3]);
            if (!band || !gain) {
                parse_error = invalidArgument(
                    "eq band requires numeric band and gain.",
                    "band",
                    "integer band index and integer gain dB",
                    "lofibox eq band <index> <gain>");
                return std::nullopt;
            }
            return command(lofibox::runtime::RuntimeCommandKind::EqSetBand, lofibox::runtime::RuntimeCommandPayload::eqSetBand(*band, *gain));
        }
    }
    if (first == "remix") {
        return command(lofibox::runtime::RuntimeCommandKind::AudioEffectCycle, lofibox::runtime::RuntimeCommandPayload::audioEffectCycle());
    }
    if (first == "remote" && p.size() >= 2 && p[1] == "reconnect") {
        return command(lofibox::runtime::RuntimeCommandKind::RemoteReconnect);
    }
    if (first == "runtime" && p.size() >= 2) {
        if (p[1] == "reload") return command(lofibox::runtime::RuntimeCommandKind::RuntimeReload);
        if (p[1] == "shutdown") return command(lofibox::runtime::RuntimeCommandKind::RuntimeShutdown);
        if (p[1] == "settings" && p.size() >= 3 && p[2] == "apply") {
            const auto output = p.size() >= 4 ? std::string{p[3]} : std::string{};
            const auto network = p.size() >= 5 ? std::string{p[4]} : std::string{};
            const auto sleep = p.size() >= 6 ? std::string{p[5]} : std::string{};
            return command(lofibox::runtime::RuntimeCommandKind::SettingsApplyLive, lofibox::runtime::RuntimeCommandPayload::settingsApplyLive(output, network, sleep));
        }
    }
    return std::nullopt;
}

std::optional<lofibox::runtime::RuntimeQuery> buildQuery(const ParsedRuntimeArgs& args)
{
    const auto& p = args.positional;
    if (p.empty()) {
        return std::nullopt;
    }
    if (p[0] == "now") {
        return lofibox::runtime::RuntimeQuery{lofibox::runtime::RuntimeQueryKind::FullSnapshot, lofibox::runtime::CommandOrigin::RuntimeCli, correlation("now")};
    }
    if (p[0] == "queue" && p.size() >= 2 && p[1] == "show") {
        return lofibox::runtime::RuntimeQuery{lofibox::runtime::RuntimeQueryKind::QueueSnapshot, lofibox::runtime::CommandOrigin::RuntimeCli, correlation("queue")};
    }
    if (p[0] == "eq" && p.size() >= 2 && p[1] == "show") {
        return lofibox::runtime::RuntimeQuery{lofibox::runtime::RuntimeQueryKind::EqSnapshot, lofibox::runtime::CommandOrigin::RuntimeCli, correlation("eq")};
    }
    if (p[0] != "runtime") {
        return std::nullopt;
    }
    if (p.size() == 1 || p[1] == "status") {
        return lofibox::runtime::RuntimeQuery{lofibox::runtime::RuntimeQueryKind::FullSnapshot, lofibox::runtime::CommandOrigin::RuntimeCli, correlation("status")};
    }
    if (p[1] == "playback") {
        return lofibox::runtime::RuntimeQuery{lofibox::runtime::RuntimeQueryKind::PlaybackSnapshot, lofibox::runtime::CommandOrigin::RuntimeCli, correlation("playback")};
    }
    if (p[1] == "queue") {
        return lofibox::runtime::RuntimeQuery{lofibox::runtime::RuntimeQueryKind::QueueSnapshot, lofibox::runtime::CommandOrigin::RuntimeCli, correlation("queue")};
    }
    if (p[1] == "eq") {
        return lofibox::runtime::RuntimeQuery{lofibox::runtime::RuntimeQueryKind::EqSnapshot, lofibox::runtime::CommandOrigin::RuntimeCli, correlation("eq")};
    }
    if (p[1] == "remote") {
        return lofibox::runtime::RuntimeQuery{lofibox::runtime::RuntimeQueryKind::RemoteSessionSnapshot, lofibox::runtime::CommandOrigin::RuntimeCli, correlation("remote")};
    }
    if (p[1] == "settings") {
        return lofibox::runtime::RuntimeQuery{lofibox::runtime::RuntimeQueryKind::SettingsSnapshot, lofibox::runtime::CommandOrigin::RuntimeCli, correlation("settings")};
    }
    return std::nullopt;
}

} // namespace

std::optional<int> runRuntimeCliCommand(int argc, char** argv, std::ostream& out, std::ostream& err)
{
    const auto args = parseArgs(argc, argv);
    if (args.positional.empty() || !isRuntimeCommand(args.positional.front())) {
        return std::nullopt;
    }
    if (args.positional.front() == "remote"
        && (args.positional.size() < 2U || args.positional[1] != "reconnect")) {
        return std::nullopt;
    }
    const auto format = outputOptions(args);
    if (hasHelpRequest(args)) {
        if (format.json || hasSchemaRequest(args)) {
            printRuntimeSchema(args, out);
        } else {
            printRuntimeHelp(args, out);
        }
        return 0;
    }
    if (hasSchemaRequest(args)) {
        printRuntimeSchema(args, out);
        return 0;
    }

    lofibox::runtime::UnixSocketRuntimeCommandClient client{
        args.socket_path ? std::filesystem::path{*args.socket_path} : lofibox::runtime::defaultRuntimeSocketPath()};

    if (const auto query = buildQuery(args)) {
        const auto snapshot = client.query(*query);
        if (!client.lastError().empty()) {
            printCliError(err, format, CliStructuredError{
                "RUNTIME_UNREACHABLE",
                client.lastError(),
                "--runtime-socket",
                "reachable LoFiBox runtime socket",
                static_cast<int>(CliExitCode::Network),
                "lofibox runtime status",
                {}});
            return static_cast<int>(CliExitCode::Network);
        }
        switch (query->kind) {
        case lofibox::runtime::RuntimeQueryKind::PlaybackSnapshot:
            if (!validateObjectFields(format, playbackFields(snapshot.playback), "runtime playback", err)) return static_cast<int>(CliExitCode::Usage);
            printPlayback(snapshot.playback, format, out);
            break;
        case lofibox::runtime::RuntimeQueryKind::QueueSnapshot:
            if (!validateObjectFields(format, queueFields(snapshot.queue), "runtime queue", err)) return static_cast<int>(CliExitCode::Usage);
            printQueue(snapshot.queue, format, out);
            break;
        case lofibox::runtime::RuntimeQueryKind::EqSnapshot:
            if (!validateObjectFields(format, eqFields(snapshot.eq), "runtime eq", err)) return static_cast<int>(CliExitCode::Usage);
            printEq(snapshot.eq, format, out);
            break;
        case lofibox::runtime::RuntimeQueryKind::RemoteSessionSnapshot:
            if (!validateObjectFields(format, remoteFields(snapshot.remote), "runtime remote", err)) return static_cast<int>(CliExitCode::Usage);
            printRemote(snapshot.remote, format, out);
            break;
        case lofibox::runtime::RuntimeQueryKind::SettingsSnapshot:
            if (!validateObjectFields(format, settingsFields(snapshot.settings), "runtime settings", err)) return static_cast<int>(CliExitCode::Usage);
            printSettings(snapshot.settings, format, out);
            break;
        case lofibox::runtime::RuntimeQueryKind::FullSnapshot:
            if (!validateFullFields(format, snapshot, err)) return static_cast<int>(CliExitCode::Usage);
            printFull(snapshot, format, out);
            break;
        }
        return 0;
    }

    std::optional<CliStructuredError> parse_error{};
    const auto command_value = buildCommand(args, parse_error);
    if (!command_value) {
        if (parse_error) {
            printCliError(err, format, *parse_error);
        } else {
            printCliError(err, format, CliStructuredError{
                "UNKNOWN_COMMAND",
                "Unknown runtime command.",
                "",
                "known runtime command",
                static_cast<int>(CliExitCode::Usage),
                "lofibox runtime --help",
                {}});
        }
        return static_cast<int>(CliExitCode::Usage);
    }
    const auto result = client.dispatch(*command_value);
    if (!client.lastError().empty()) {
        printCliError(err, format, CliStructuredError{
            "RUNTIME_UNREACHABLE",
            client.lastError(),
            "--runtime-socket",
            "reachable LoFiBox runtime socket",
            static_cast<int>(CliExitCode::Network),
            "lofibox runtime status",
            {}});
        return static_cast<int>(CliExitCode::Network);
    }
    printResult(result, format, out);
    if (!result.accepted) {
        return static_cast<int>(CliExitCode::Usage);
    }
    if (!result.applied && command_value->kind == lofibox::runtime::RuntimeCommandKind::PlaybackPlay) {
        return static_cast<int>(CliExitCode::Playback);
    }
    return 0;
}

} // namespace lofibox::cli
