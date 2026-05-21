// SPDX-License-Identifier: GPL-3.0-or-later

#include "cli/direct_cli.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <initializer_list>
#include <iostream>
#include <optional>
#include <ostream>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "app/remote_profile_store.h"
#include "application/app_service_host.h"
#include "application/app_service_registry.h"
#include "application/credential_command_service.h"
#include "application/source_profile_command_service.h"
#include "cli/cli_format.h"
#include "remote/common/remote_catalog_model.h"

namespace lofibox::cli {
namespace {

struct CliOption {
    std::string name{};
    std::string value{};
};

struct ParsedArgs {
    std::vector<std::string_view> positional{};
    std::vector<CliOption> options{};
};

bool isDirectCommand(std::string_view value) noexcept
{
    return value == "source"
        || value == "library"
        || value == "search"
        || value == "remote"
        || value == "metadata"
        || value == "lyrics"
        || value == "artwork"
        || value == "fingerprint"
        || value == "doctor"
        || value == "cache"
        || value == "credentials";
}

ParsedArgs parseArgs(int argc, char** argv)
{
    ParsedArgs result{};
    for (int index = 1; index < argc; ++index) {
        std::string_view current{argv[index]};
        if (current.rfind("--", 0) == 0) {
            const auto eq = current.find('=');
            if (eq != std::string_view::npos) {
                result.options.push_back({std::string{current.substr(2, eq - 2)}, std::string{current.substr(eq + 1)}});
            } else if ((index + 1) < argc && std::string_view{argv[index + 1]}.rfind("-", 0) != 0) {
                result.options.push_back({std::string{current.substr(2)}, argv[index + 1]});
                ++index;
            } else {
                result.options.push_back({std::string{current.substr(2)}, "true"});
            }
            continue;
        }
        result.positional.push_back(current);
    }
    return result;
}

std::optional<std::string> optionValue(const ParsedArgs& args, std::string_view name)
{
    for (const auto& option : args.options) {
        if (option.name == name) {
            return option.value;
        }
    }
    return std::nullopt;
}

std::optional<std::string> optionValueAny(const ParsedArgs& args, std::initializer_list<std::string_view> names)
{
    for (const auto name : names) {
        if (const auto value = optionValue(args, name)) {
            return value;
        }
    }
    return std::nullopt;
}

bool optionPresent(const ParsedArgs& args, std::string_view name)
{
    return optionValue(args, name).has_value();
}

bool helpRequested(const ParsedArgs& args)
{
    if (optionPresent(args, "help")) {
        return true;
    }
    for (const auto item : args.positional) {
        if (item == "help" || item == "-h") {
            return true;
        }
    }
    return false;
}

CliOutputOptions outputOptions(const ParsedArgs& args)
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

int directError(
    const ParsedArgs& args,
    std::ostream& err,
    CliExitCode exit_code,
    std::string message,
    std::string code = "INVALID_ARGUMENT",
    std::string argument = {},
    std::string expected = {},
    std::string usage = {})
{
    printCliError(err, outputOptions(args), CliStructuredError{
        std::move(code),
        std::move(message),
        std::move(argument),
        std::move(expected),
        static_cast<int>(exit_code),
        std::move(usage),
        {}});
    return static_cast<int>(exit_code);
}

std::optional<std::size_t> findProfileById(const std::vector<app::RemoteServerProfile>& profiles, std::string_view id)
{
    const auto found = std::find_if(profiles.begin(), profiles.end(), [id](const app::RemoteServerProfile& profile) {
        return profile.id == id;
    });
    if (found == profiles.end()) {
        return std::nullopt;
    }
    return static_cast<std::size_t>(std::distance(profiles.begin(), found));
}

std::optional<std::size_t> findProfileByIdOrCredentialRef(
    const std::vector<app::RemoteServerProfile>& profiles,
    std::string_view id_or_ref)
{
    const auto found = std::find_if(profiles.begin(), profiles.end(), [id_or_ref](const app::RemoteServerProfile& profile) {
        return profile.id == id_or_ref || profile.credential_ref.id == id_or_ref;
    });
    if (found == profiles.end()) {
        return std::nullopt;
    }
    return static_cast<std::size_t>(std::distance(profiles.begin(), found));
}

std::string boolLabel(bool value)
{
    return value ? "true" : "false";
}

std::string optionalLabel(const std::optional<std::string>& value)
{
    return value && !value->empty() ? *value : "-";
}

std::string optionalNumberLabel(const std::optional<int>& value)
{
    return value ? std::to_string(*value) : "0";
}

std::string previewText(const std::optional<std::string>& value, std::size_t limit = 160U)
{
    if (!value || value->empty()) {
        return "-";
    }
    std::string preview = *value;
    for (auto& ch : preview) {
        if (ch == '\n' || ch == '\r' || ch == '\t') {
            ch = ' ';
        }
    }
    if (preview.size() > limit) {
        preview.resize(limit);
        preview += "...";
    }
    return preview;
}

std::string lineCountLabel(const std::optional<std::string>& value)
{
    if (!value || value->empty()) {
        return "0";
    }
    int lines = 1;
    for (const auto ch : *value) {
        if (ch == '\n') {
            ++lines;
        }
    }
    return std::to_string(lines);
}

bool coreMetadataPresent(const app::TrackMetadata& metadata)
{
    return metadata.title.has_value()
        || metadata.artist.has_value()
        || metadata.album.has_value()
        || metadata.genre.has_value()
        || metadata.composer.has_value()
        || metadata.duration_seconds.has_value();
}

std::optional<std::filesystem::path> targetPathArg(
    const ParsedArgs& args,
    std::ostream& err,
    std::string_view command,
    std::string_view verb)
{
    if (args.positional.size() < 3U) {
        (void)directError(
            args,
            err,
            CliExitCode::Usage,
            std::string{command} + " " + std::string{verb} + " requires a path.",
            "INVALID_ARGUMENT",
            "path",
            "audio file path",
            "lofibox " + std::string{command} + " " + std::string{verb} + " <path>");
        return std::nullopt;
    }
    std::filesystem::path path{std::string{args.positional[2]}};
    if (!std::filesystem::exists(path)) {
        (void)directError(
            args,
            err,
            CliExitCode::NotFound,
            "file not found: " + path.string(),
            "NOT_FOUND",
            "path",
            "existing audio file path",
            "lofibox " + std::string{command} + " " + std::string{verb} + " <path>");
        return std::nullopt;
    }
    return path;
}

bool parseBool(std::string_view value, bool fallback)
{
    if (value == "true" || value == "1" || value == "yes" || value == "on") {
        return true;
    }
    if (value == "false" || value == "0" || value == "no" || value == "off") {
        return false;
    }
    return fallback;
}

std::string stateLabel(app::LibraryIndexState state)
{
    switch (state) {
    case app::LibraryIndexState::Uninitialized: return "UNINITIALIZED";
    case app::LibraryIndexState::Loading: return "LOADING";
    case app::LibraryIndexState::Ready: return "READY";
    case app::LibraryIndexState::Degraded: return "DEGRADED";
    }
    return "UNKNOWN";
}

std::string bytesLabel(std::uintmax_t bytes)
{
    if (bytes >= 1024ULL * 1024ULL * 1024ULL) {
        return std::to_string(bytes / (1024ULL * 1024ULL * 1024ULL)) + "G";
    }
    if (bytes >= 1024ULL * 1024ULL) {
        return std::to_string(bytes / (1024ULL * 1024ULL)) + "M";
    }
    if (bytes >= 1024ULL) {
        return std::to_string(bytes / 1024ULL) + "K";
    }
    return std::to_string(bytes) + "B";
}

std::vector<std::filesystem::path> rootsFromOption(const ParsedArgs& args)
{
    std::vector<std::filesystem::path> roots{};
    if (const auto root = optionValue(args, "root")) {
        roots.emplace_back(*root);
    }
    return roots;
}

std::string stdinLine()
{
    std::string value{};
    std::getline(std::cin, value);
    return value;
}

std::string catalogNodeKindLabel(app::RemoteCatalogNodeKind kind)
{
    switch (kind) {
    case app::RemoteCatalogNodeKind::Root: return "root";
    case app::RemoteCatalogNodeKind::Artists: return "artists";
    case app::RemoteCatalogNodeKind::Artist: return "artist";
    case app::RemoteCatalogNodeKind::Albums: return "albums";
    case app::RemoteCatalogNodeKind::Album: return "album";
    case app::RemoteCatalogNodeKind::Tracks: return "tracks";
    case app::RemoteCatalogNodeKind::Genres: return "genres";
    case app::RemoteCatalogNodeKind::Genre: return "genre";
    case app::RemoteCatalogNodeKind::Playlists: return "playlists";
    case app::RemoteCatalogNodeKind::Playlist: return "playlist";
    case app::RemoteCatalogNodeKind::Folders: return "folders";
    case app::RemoteCatalogNodeKind::Folder: return "folder";
    case app::RemoteCatalogNodeKind::Favorites: return "favorites";
    case app::RemoteCatalogNodeKind::RecentlyAdded: return "recently-added";
    case app::RemoteCatalogNodeKind::RecentlyPlayed: return "recently-played";
    case app::RemoteCatalogNodeKind::Stations: return "stations";
    }
    return "unknown";
}

std::optional<app::RemoteCatalogNode> rootNodeForPath(std::string_view path)
{
    auto normalized = std::string{path};
    while (!normalized.empty() && normalized.front() == '/') {
        normalized.erase(normalized.begin());
    }
    if (normalized.empty()) {
        return app::RemoteCatalogNode{};
    }
    for (auto node : remote::RemoteCatalogMap::rootNodes()) {
        if (node.id == normalized) {
            return node;
        }
    }
    return std::nullopt;
}

void printDirectHelp(std::ostream& out)
{
    out << "Direct LoFiBox commands:\n"
        << "  Global: [--json] [--porcelain] [--fields a,b] [--quiet]\n"
        << "  lofibox source list\n"
        << "  lofibox source show <profile-id>\n"
        << "  lofibox source add <kind> --id <id> --name <label> --base-url <url> [--username <user>] [--credential-ref <ref>]\n"
        << "  lofibox source update <id> [--name <label>] [--base-url <url>] [--username <user>] [--credential-ref <ref>]\n"
        << "  lofibox source remove <profile-id>\n"
        << "  lofibox source probe <id>\n"
        << "  lofibox source auth-status <profile-id>\n"
        << "  lofibox source capabilities <profile-id>\n"
        << "  lofibox source local-root list\n"
        << "  lofibox source local-root add <path> [--name <label>]\n"
        << "  lofibox source local-root remove|enable|disable <id-or-path>\n"
        << "  lofibox credentials list\n"
        << "  lofibox credentials show-ref <profile-id-or-ref>\n"
        << "  lofibox credentials set <profile-id-or-ref> [--username <user>] [--password-stdin] [--token-stdin]\n"
        << "  Example: printf '%s\\n' \"$LOFIBOX_TOKEN\" | lofibox credentials set jellyfin-home --token-stdin\n"
        << "  Avoid argv secrets such as --password <secret> or --token <secret>; they can leak through shell history and process inspection.\n"
        << "  lofibox credentials status|validate <profile-id-or-ref>\n"
        << "  lofibox credentials delete <profile-id-or-ref>\n"
        << "  lofibox library scan [path...]\n"
        << "  lofibox library root list\n"
        << "  lofibox library root add <path> [--name <label>]\n"
        << "  lofibox library root remove|enable|disable <id-or-path>\n"
        << "  lofibox library status [--root <path>]\n"
        << "  lofibox library list tracks|albums|artists|genres|composers|compilations [--root <path>]\n"
        << "  lofibox library query tracks|albums [--artist <name>] [--album-id <id>] [--genre <name>] [--root <path>]\n"
        << "  lofibox library query <text> [--root <path>]\n"
        << "  lofibox search <text> [--local] [--source <profile-id>] [--all]\n"
        << "  lofibox remote browse <profile-id> [/artists|/albums|/playlists|/stations]\n"
        << "  lofibox remote recent <profile-id>\n"
        << "  lofibox remote search <profile-id> <text>\n"
        << "  lofibox remote resolve|stream-info <profile-id> <item-id>\n"
        << "  lofibox metadata show|lookup|apply <path>\n"
        << "  lofibox lyrics show|lookup|apply <path>\n"
        << "  lofibox artwork show|lookup <path>\n"
        << "  lofibox fingerprint show|generate|match <path>\n"
        << "  metadata lookup/apply and lyrics lookup/apply may update governed local cache and write missing tags when the authority policy allows it.\n"
        << "  lofibox cache status\n"
        << "  lofibox cache purge|clear\n"
        << "  lofibox cache gc\n"
        << "  lofibox doctor\n";
}

void applyProfileOptions(
    const ParsedArgs& args,
    application::SourceProfileCommandService source_profiles,
    app::RemoteServerProfile& profile,
    std::size_t profile_count)
{
    if (const auto value = optionValue(args, "name")) {
        (void)source_profiles.updateTextField(profile, application::SourceProfileTextField::Label, *value, profile_count);
    }
    if (const auto value = optionValueAny(args, {"base-url", "url"})) {
        (void)source_profiles.updateTextField(profile, application::SourceProfileTextField::Address, *value, profile_count);
    }
    if (const auto value = optionValueAny(args, {"username", "user"})) {
        (void)source_profiles.updateTextField(profile, application::SourceProfileTextField::Username, *value, profile_count);
    }
    if (const auto value = optionValue(args, "credential-ref")) {
        profile.credential_ref.id = *value;
    }
    if (optionPresent(args, "insecure")) {
        profile.tls_policy.verify_peer = false;
    }
    if (optionPresent(args, "allow-self-signed")) {
        profile.tls_policy.allow_self_signed = parseBool(*optionValue(args, "allow-self-signed"), true);
        if (profile.tls_policy.allow_self_signed) {
            profile.tls_policy.verify_peer = false;
        }
    }
    if (const auto value = optionValue(args, "verify-peer")) {
        profile.tls_policy.verify_peer = parseBool(*value, profile.tls_policy.verify_peer);
        if (profile.tls_policy.verify_peer) {
            profile.tls_policy.allow_self_signed = false;
        }
    }
}

application::CredentialSecretPatch credentialPatchFromOptions(const ParsedArgs& args)
{
    application::CredentialSecretPatch patch{};
    if (const auto value = optionValueAny(args, {"username", "user"})) {
        patch.username = *value;
    }
    if (const auto value = optionValue(args, "password")) {
        patch.password = *value;
    }
    if (const auto value = optionValue(args, "token")) {
        patch.api_token = *value;
    }
    if (optionPresent(args, "password-stdin")) {
        patch.password = stdinLine();
    }
    if (optionPresent(args, "token-stdin")) {
        patch.api_token = stdinLine();
    }
    return patch;
}

bool hasCredentialPatch(const application::CredentialSecretPatch& patch) noexcept
{
    return !patch.username.empty() || !patch.password.empty() || !patch.api_token.empty();
}

CliFields profileFields(const app::RemoteServerProfile& profile, application::SourceProfileCommandService source_profiles)
{
    const auto local_root = source_profiles.localRootPath(profile);
    return {
        {"id", profile.id},
        {"kind", app::remoteServerKindToString(profile.kind)},
        {"name", source_profiles.profileLabel(profile)},
        {"base_url", profile.base_url.empty() ? "-" : profile.base_url},
        {"local_root", local_root.empty() ? "-" : local_root.string()},
        {"enabled", boolLabel(profile.enabled)},
        {"default_eligible", boolLabel(profile.default_eligible)},
        {"username", source_profiles.usernameLabel(profile)},
        {"credential_ref", profile.credential_ref.id.empty() ? "-" : profile.credential_ref.id},
        {"readiness", source_profiles.readiness(profile)},
        {"permission", source_profiles.permissionLabel(profile.kind)},
        {"verify_peer", boolLabel(profile.tls_policy.verify_peer)},
        {"allow_self_signed", boolLabel(profile.tls_policy.allow_self_signed)},
    };
}

CliFields credentialStatusFields(const app::RemoteServerProfile& profile, const application::CredentialStatus& status)
{
    return {
        {"profile_id", profile.id},
        {"credential_ref", status.attached ? status.ref_id : "NONE"},
        {"username", status.has_username ? "SET" : "MISSING"},
        {"password", status.has_password ? "SET" : "MISSING"},
        {"token", status.has_api_token ? "SET" : "MISSING"},
    };
}

CliFields cacheStatusFields(const application::CacheStatusResult& result)
{
    return {
        {"root", result.root.string()},
        {"entries", std::to_string(result.usage.entry_count)},
        {"bytes", std::to_string(result.usage.total_bytes)},
        {"size", bytesLabel(result.usage.total_bytes)},
    };
}

CliFields trackFields(const app::TrackRecord& track)
{
    return {
        {"id", std::to_string(track.id)},
        {"title", track.title},
        {"artist", track.artist},
        {"album", track.album},
        {"genre", track.genre},
        {"composer", track.composer},
        {"duration", std::to_string(track.duration_seconds)},
        {"play_count", std::to_string(track.play_count)},
        {"added_time", std::to_string(track.added_time)},
        {"last_played", std::to_string(track.last_played)},
        {"path", track.path.string()},
    };
}

CliFields mediaItemFields(const app::MediaItem& item, std::string_view group)
{
    return {
        {"group", std::string{group}},
        {"id", item.id},
        {"title", item.title},
        {"artist", item.artist},
        {"album", item.album},
        {"source", item.source.source_label.empty() ? "local" : item.source.source_label},
    };
}

CliFields remoteNodeFields(const app::RemoteCatalogNode& node)
{
    return {
        {"kind", catalogNodeKindLabel(node.kind)},
        {"id", node.id},
        {"title", node.title},
        {"subtitle", node.subtitle},
        {"playable", boolLabel(node.playable)},
        {"browsable", boolLabel(node.browsable)},
        {"artist", node.artist},
        {"album", node.album},
        {"duration", std::to_string(node.duration_seconds)},
    };
}

CliFields remoteTrackFields(const app::RemoteTrack& track, std::string_view group)
{
    return {
        {"group", std::string{group}},
        {"id", track.id},
        {"title", track.title},
        {"artist", track.artist},
        {"album", track.album},
        {"album_id", track.album_id},
        {"duration", std::to_string(track.duration_seconds)},
        {"source", track.source_label},
    };
}

CliFields metadataFields(
    const std::filesystem::path& path,
    const app::TrackMetadata& metadata,
    std::string_view provider,
    std::string_view status)
{
    return {
        {"status", std::string{status}},
        {"path", path.string()},
        {"provider", std::string{provider}},
        {"title", optionalLabel(metadata.title)},
        {"artist", optionalLabel(metadata.artist)},
        {"album", optionalLabel(metadata.album)},
        {"genre", optionalLabel(metadata.genre)},
        {"composer", optionalLabel(metadata.composer)},
        {"duration", optionalNumberLabel(metadata.duration_seconds)},
        {"found", boolLabel(coreMetadataPresent(metadata))},
    };
}

CliFields lyricsFields(
    const std::filesystem::path& path,
    const app::TrackLyrics& lyrics,
    std::string_view provider,
    std::string_view status)
{
    return {
        {"status", std::string{status}},
        {"path", path.string()},
        {"provider", std::string{provider}},
        {"source", lyrics.source.empty() ? "-" : lyrics.source},
        {"plain_available", boolLabel(lyrics.plain.has_value() && !lyrics.plain->empty())},
        {"synced_available", boolLabel(lyrics.synced.has_value() && !lyrics.synced->empty())},
        {"plain_lines", lineCountLabel(lyrics.plain)},
        {"synced_lines", lineCountLabel(lyrics.synced)},
        {"plain_preview", previewText(lyrics.plain)},
        {"synced_preview", previewText(lyrics.synced)},
    };
}

CliFields identityFields(
    const std::filesystem::path& path,
    const app::TrackIdentity& identity,
    std::string_view provider,
    std::string_view status)
{
    return {
        {"status", std::string{status}},
        {"path", path.string()},
        {"provider", std::string{provider}},
        {"found", boolLabel(identity.found)},
        {"source", identity.source.empty() ? "-" : identity.source},
        {"confidence", std::to_string(identity.confidence)},
        {"fingerprint", identity.fingerprint.empty() ? "-" : identity.fingerprint},
        {"recording_mbid", identity.recording_mbid.empty() ? "-" : identity.recording_mbid},
        {"release_mbid", identity.release_mbid.empty() ? "-" : identity.release_mbid},
        {"release_group_mbid", identity.release_group_mbid.empty() ? "-" : identity.release_group_mbid},
        {"title", optionalLabel(identity.metadata.title)},
        {"artist", optionalLabel(identity.metadata.artist)},
        {"album", optionalLabel(identity.metadata.album)},
        {"audio_fingerprint_verified", boolLabel(identity.audio_fingerprint_verified)},
    };
}

int runMetadataCommand(const ParsedArgs& args, application::AppServiceRegistry&, app::RuntimeServices& services, std::ostream& out, std::ostream& err)
{
    if (args.positional.size() < 2U || args.positional[1] == "help") {
        printDirectHelp(out);
        return 0;
    }
    const auto format = outputOptions(args);
    const auto verb = args.positional[1];
    if (verb != "show" && verb != "lookup" && verb != "apply") {
        return directError(args, err, CliExitCode::Usage, "unknown metadata command: " + std::string{verb}, "UNKNOWN_COMMAND", "verb", "metadata subcommand", "lofibox metadata show|lookup|apply <path>");
    }
    if (args.positional.size() < 3U) {
        return directError(args, err, CliExitCode::Usage, "metadata " + std::string{verb} + " requires a path.", "INVALID_ARGUMENT", "path", "audio file path", "lofibox metadata " + std::string{verb} + " <path>");
    }
    const auto path = targetPathArg(args, err, "metadata", verb);
    if (!path) {
        return static_cast<int>(CliExitCode::NotFound);
    }
    if (!services.metadata.metadata_provider || !services.metadata.metadata_provider->available()) {
        return directError(args, err, CliExitCode::Persistence, "metadata provider unavailable.", "UNAVAILABLE", "metadata", "available metadata provider");
    }
    const auto mode = verb == "show" ? app::MetadataReadMode::LocalOnly : app::MetadataReadMode::AllowOnline;
    const auto metadata = services.metadata.metadata_provider->read(*path, mode);
    printObject(out, format, metadataFields(
        *path,
        metadata,
        services.metadata.metadata_provider->displayName(),
        verb == "show" ? "READ" : (verb == "apply" ? "APPLIED" : "LOOKED_UP")));
    return coreMetadataPresent(metadata) ? 0 : static_cast<int>(CliExitCode::NotFound);
}

int runLyricsCommand(const ParsedArgs& args, application::AppServiceRegistry&, app::RuntimeServices& services, std::ostream& out, std::ostream& err)
{
    if (args.positional.size() < 2U || args.positional[1] == "help") {
        printDirectHelp(out);
        return 0;
    }
    const auto format = outputOptions(args);
    const auto verb = args.positional[1];
    if (verb != "show" && verb != "lookup" && verb != "apply") {
        return directError(args, err, CliExitCode::Usage, "unknown lyrics command: " + std::string{verb}, "UNKNOWN_COMMAND", "verb", "lyrics subcommand", "lofibox lyrics show|lookup|apply <path>");
    }
    if (args.positional.size() < 3U) {
        return directError(args, err, CliExitCode::Usage, "lyrics " + std::string{verb} + " requires a path.", "INVALID_ARGUMENT", "path", "audio file path", "lofibox lyrics " + std::string{verb} + " <path>");
    }
    const auto path = targetPathArg(args, err, "lyrics", verb);
    if (!path) {
        return static_cast<int>(CliExitCode::NotFound);
    }
    if (!services.metadata.lyrics_provider || !services.metadata.lyrics_provider->available()) {
        return directError(args, err, CliExitCode::Persistence, "lyrics provider unavailable.", "UNAVAILABLE", "lyrics", "available lyrics provider");
    }
    app::TrackMetadata metadata{};
    if (services.metadata.metadata_provider && services.metadata.metadata_provider->available()) {
        metadata = services.metadata.metadata_provider->read(
            *path,
            verb == "show" ? app::MetadataReadMode::LocalOnly : app::MetadataReadMode::AllowOnline);
    }
    const auto lyrics = services.metadata.lyrics_provider->fetch(*path, metadata);
    const bool found = (lyrics.plain && !lyrics.plain->empty()) || (lyrics.synced && !lyrics.synced->empty());
    printObject(out, format, lyricsFields(
        *path,
        lyrics,
        services.metadata.lyrics_provider->displayName(),
        found ? (verb == "show" ? "FOUND" : (verb == "apply" ? "APPLIED" : "LOOKED_UP")) : "MISSING"));
    return found ? 0 : static_cast<int>(CliExitCode::NotFound);
}

int runArtworkCommand(const ParsedArgs& args, application::AppServiceRegistry&, app::RuntimeServices& services, std::ostream& out, std::ostream& err)
{
    if (args.positional.size() < 2U || args.positional[1] == "help") {
        printDirectHelp(out);
        return 0;
    }
    const auto format = outputOptions(args);
    const auto verb = args.positional[1];
    if (verb != "show" && verb != "lookup") {
        return directError(args, err, CliExitCode::Usage, "unknown artwork command: " + std::string{verb}, "UNKNOWN_COMMAND", "verb", "artwork subcommand", "lofibox artwork show|lookup <path>");
    }
    if (args.positional.size() < 3U) {
        return directError(args, err, CliExitCode::Usage, "artwork " + std::string{verb} + " requires a path.", "INVALID_ARGUMENT", "path", "audio file path", "lofibox artwork " + std::string{verb} + " <path>");
    }
    const auto path = targetPathArg(args, err, "artwork", verb);
    if (!path) {
        return static_cast<int>(CliExitCode::NotFound);
    }
    if (!services.metadata.artwork_provider || !services.metadata.artwork_provider->available()) {
        return directError(args, err, CliExitCode::Persistence, "artwork provider unavailable.", "UNAVAILABLE", "artwork", "available artwork provider");
    }
    const auto artwork = services.metadata.artwork_provider->read(
        *path,
        verb == "show" ? app::ArtworkReadMode::LocalOnly : app::ArtworkReadMode::AllowOnline);
    printObject(out, format, {
        {"status", artwork ? "FOUND" : "MISSING"},
        {"path", path->string()},
        {"provider", services.metadata.artwork_provider->displayName()},
        {"available", boolLabel(artwork.has_value())},
        {"width", artwork ? std::to_string(artwork->width()) : "0"},
        {"height", artwork ? std::to_string(artwork->height()) : "0"},
    });
    return artwork ? 0 : static_cast<int>(CliExitCode::NotFound);
}

int runFingerprintCommand(const ParsedArgs& args, application::AppServiceRegistry&, app::RuntimeServices& services, std::ostream& out, std::ostream& err)
{
    if (args.positional.size() < 2U || args.positional[1] == "help") {
        printDirectHelp(out);
        return 0;
    }
    const auto format = outputOptions(args);
    const auto verb = args.positional[1];
    if (verb != "show" && verb != "generate" && verb != "match") {
        return directError(args, err, CliExitCode::Usage, "unknown fingerprint command: " + std::string{verb}, "UNKNOWN_COMMAND", "verb", "fingerprint subcommand", "lofibox fingerprint show|generate|match <path>");
    }
    if (args.positional.size() < 3U) {
        return directError(args, err, CliExitCode::Usage, "fingerprint " + std::string{verb} + " requires a path.", "INVALID_ARGUMENT", "path", "audio file path", "lofibox fingerprint " + std::string{verb} + " <path>");
    }
    const auto path = targetPathArg(args, err, "fingerprint", verb);
    if (!path) {
        return static_cast<int>(CliExitCode::NotFound);
    }
    if (!services.metadata.track_identity_provider || !services.metadata.track_identity_provider->available()) {
        return directError(args, err, CliExitCode::Persistence, "fingerprint provider unavailable.", "UNAVAILABLE", "fingerprint", "available fingerprint/identity provider");
    }
    app::TrackMetadata metadata{};
    if (services.metadata.metadata_provider && services.metadata.metadata_provider->available()) {
        metadata = services.metadata.metadata_provider->read(*path, app::MetadataReadMode::LocalOnly);
    }
    const auto identity = services.metadata.track_identity_provider->resolve(*path, metadata);
    printObject(out, format, identityFields(
        *path,
        identity,
        services.metadata.track_identity_provider->displayName(),
        identity.found ? (verb == "match" ? "MATCHED" : "GENERATED") : "MISSING"));
    return identity.found || !identity.fingerprint.empty() ? 0 : static_cast<int>(CliExitCode::NotFound);
}

int runLocalRootCommand(
    const ParsedArgs& args,
    application::AppServiceRegistry& registry,
    std::ostream& out,
    std::ostream& err,
    std::size_t verb_index,
    std::string usage_prefix)
{
    const auto format = outputOptions(args);
    if (args.positional.size() <= verb_index || args.positional[verb_index] == "help") {
        printDirectHelp(out);
        return 0;
    }

    const auto verb = args.positional[verb_index];
    if (verb == "list") {
        std::vector<CliFields> rows{};
        for (const auto& profile : registry.sourceProfiles().listLocalRoots()) {
            rows.push_back(profileFields(profile, registry.sourceProfiles()));
        }
        printPorcelainRows(out, format, rows, "NO LOCAL ROOTS");
        return 0;
    }

    if (verb == "add") {
        if (args.positional.size() <= verb_index + 1U) {
            return directError(args, err, CliExitCode::Usage, "local-root add requires a path.", "INVALID_ARGUMENT", "path", "local media directory", usage_prefix + " add <path> [--name <label>]");
        }
        const auto result = registry.sourceProfiles().addLocalRoot(
            std::filesystem::path{std::string{args.positional[verb_index + 1U]}},
            optionValue(args, "name"));
        if (!result.command.accepted) {
            return directError(args, err, CliExitCode::Persistence, result.command.summary, result.command.code);
        }
        registry.libraryMutations().markConfiguredLibraryStale();
        auto fields = profileFields(result.profile, registry.sourceProfiles());
        fields.insert(fields.begin(), {"status", "ADDED"});
        printObject(out, format, fields);
        return 0;
    }

    if (verb == "remove" || verb == "enable" || verb == "disable") {
        if (args.positional.size() <= verb_index + 1U) {
            return directError(args, err, CliExitCode::Usage, "local-root " + std::string{verb} + " requires an id or path.", "INVALID_ARGUMENT", "id-or-path", "local root id or path", usage_prefix + " " + std::string{verb} + " <id-or-path>");
        }
        const auto target = args.positional[verb_index + 1U];
        application::LocalRootCommandResult result{};
        if (verb == "remove") {
            result = registry.sourceProfiles().removeLocalRoot(target);
        } else if (verb == "enable") {
            result = registry.sourceProfiles().enableLocalRoot(target);
        } else {
            result = registry.sourceProfiles().disableLocalRoot(target);
        }
        if (!result.command.accepted) {
            return directError(
                args,
                err,
                result.command.code.find("not-found") != std::string::npos ? CliExitCode::NotFound : CliExitCode::Persistence,
                result.command.summary,
                result.command.code);
        }
        registry.libraryMutations().markConfiguredLibraryStale();
        auto fields = profileFields(result.profile, registry.sourceProfiles());
        fields.insert(fields.begin(), {"status", result.command.summary});
        printObject(out, format, fields);
        return 0;
    }

    return directError(args, err, CliExitCode::Usage, "unknown local-root command: " + std::string{verb}, "UNKNOWN_COMMAND", "verb", "local-root subcommand", usage_prefix + " list|add|remove|enable|disable");
}

int runSourceCommand(const ParsedArgs& args, application::AppServiceRegistry& registry, std::ostream& out, std::ostream& err)
{
    if (args.positional.size() < 2U || args.positional[1] == "help") {
        printDirectHelp(out);
        return 0;
    }

    const auto format = outputOptions(args);
    auto profiles = registry.sourceProfiles().loadProfiles();
    const auto verb = args.positional[1];
    if (verb == "local-root") {
        return runLocalRootCommand(args, registry, out, err, 2U, "lofibox source local-root");
    }
    if (verb == "list") {
        std::vector<CliFields> rows{};
        rows.reserve(profiles.size());
        for (const auto& profile : profiles) {
            rows.push_back(profileFields(profile, registry.sourceProfiles()));
        }
        printPorcelainRows(out, format, rows, "NO SOURCES");
        return 0;
    }

    if (verb == "show") {
        if (args.positional.size() < 3U) {
            return directError(args, err, CliExitCode::Usage, "source show requires a source id.", "INVALID_ARGUMENT", "profile-id", "source profile id", "lofibox source show <profile-id>");
        }
        const auto index = findProfileById(profiles, args.positional[2]);
        if (!index) {
            return directError(args, err, CliExitCode::NotFound, "source not found: " + std::string{args.positional[2]}, "NOT_FOUND", "profile-id", "existing source profile id", "lofibox source list");
        }
        printObject(out, format, profileFields(profiles[*index], registry.sourceProfiles()));
        return 0;
    }

    if (verb == "add") {
        if (args.positional.size() < 3U) {
            return directError(args, err, CliExitCode::Usage, "source add requires a kind.", "INVALID_ARGUMENT", "kind", "source kind", "lofibox source add <kind> --id <id> --name <name> --base-url <url>");
        }
        auto profile = registry.sourceProfiles().createProfile(app::remoteServerKindFromString(std::string{args.positional[2]}), profiles.size());
        if (const auto id = optionValue(args, "id")) {
            profile.id = *id;
            profile.credential_ref.id.clear();
            registry.sourceProfiles().ensureCredentialRef(profile, profiles.size() + 1U);
        }
        applyProfileOptions(args, registry.sourceProfiles(), profile, profiles.size() + 1U);
        auto patch = credentialPatchFromOptions(args);
        if (hasCredentialPatch(patch) && registry.sourceProfiles().supportsCredentials(profile.kind)) {
            const auto credential_result = registry.credentials().setSecret(profile, patch);
            if (!credential_result.accepted) {
                err << credential_result.summary << '\n';
                return static_cast<int>(CliExitCode::Credential);
            }
        }
        profiles.push_back(std::move(profile));
        if (!registry.sourceProfiles().persistProfiles(profiles)) {
            return directError(args, err, CliExitCode::Persistence, "failed to save source profiles.", "PERSISTENCE_FAILED");
        }
        auto fields = profileFields(profiles.back(), registry.sourceProfiles());
        fields.insert(fields.begin(), {"status", "ADDED"});
        printObject(out, format, fields);
        return 0;
    }

    if (verb == "update") {
        if (args.positional.size() < 3U) {
            return directError(args, err, CliExitCode::Usage, "source update requires a source id.", "INVALID_ARGUMENT", "profile-id", "source profile id", "lofibox source update <profile-id>");
        }
        const auto index = findProfileById(profiles, args.positional[2]);
        if (!index) {
            return directError(args, err, CliExitCode::NotFound, "source not found: " + std::string{args.positional[2]}, "NOT_FOUND", "profile-id", "existing source profile id", "lofibox source list");
        }
        auto& profile = profiles[*index];
        applyProfileOptions(args, registry.sourceProfiles(), profile, profiles.size());
        auto patch = credentialPatchFromOptions(args);
        if (hasCredentialPatch(patch) && registry.sourceProfiles().supportsCredentials(profile.kind)) {
            const auto credential_result = registry.credentials().setSecret(profile, patch);
            if (!credential_result.accepted) {
                err << credential_result.summary << '\n';
                return static_cast<int>(CliExitCode::Credential);
            }
        }
        if (!registry.sourceProfiles().persistProfiles(profiles)) {
            return directError(args, err, CliExitCode::Persistence, "failed to save source profiles.", "PERSISTENCE_FAILED");
        }
        auto fields = profileFields(profile, registry.sourceProfiles());
        fields.insert(fields.begin(), {"status", "UPDATED"});
        printObject(out, format, fields);
        return 0;
    }

    if (verb == "remove") {
        if (args.positional.size() < 3U) {
            return directError(args, err, CliExitCode::Usage, "source remove requires a source id.", "INVALID_ARGUMENT", "profile-id", "source profile id", "lofibox source remove <profile-id>");
        }
        const auto index = findProfileById(profiles, args.positional[2]);
        if (!index) {
            return directError(args, err, CliExitCode::NotFound, "source not found: " + std::string{args.positional[2]}, "NOT_FOUND", "profile-id", "existing source profile id", "lofibox source list");
        }
        const auto removed_id = profiles[*index].id;
        profiles.erase(profiles.begin() + static_cast<std::ptrdiff_t>(*index));
        if (!registry.sourceProfiles().persistProfiles(profiles)) {
            return directError(args, err, CliExitCode::Persistence, "failed to save source profiles.", "PERSISTENCE_FAILED");
        }
        printObject(out, format, {{"status", "REMOVED"}, {"id", removed_id}});
        return 0;
    }

    if (verb == "probe") {
        if (args.positional.size() < 3U) {
            return directError(args, err, CliExitCode::Usage, "source probe requires a source id.", "INVALID_ARGUMENT", "profile-id", "source profile id", "lofibox source probe <profile-id>");
        }
        const auto index = findProfileById(profiles, args.positional[2]);
        if (!index) {
            return directError(args, err, CliExitCode::NotFound, "source not found: " + std::string{args.positional[2]}, "NOT_FOUND", "profile-id", "existing source profile id", "lofibox source list");
        }
        const auto probe = registry.sourceProfiles().probe(profiles[*index], profiles.size());
        printObject(out, format, {
            {"id", profiles[*index].id},
            {"status", probe.session.available ? "ONLINE" : "OFFLINE"},
            {"server_name", probe.session.server_name},
            {"user_id", probe.session.user_id},
            {"message", probe.session.message.empty() ? probe.command.summary : probe.session.message},
        });
        return probe.session.available ? 0 : static_cast<int>(CliExitCode::Network);
    }

    if (verb == "auth-status") {
        if (args.positional.size() < 3U) {
            return directError(args, err, CliExitCode::Usage, "source auth-status requires a source id.", "INVALID_ARGUMENT", "profile-id", "source profile id", "lofibox source auth-status <profile-id>");
        }
        const auto index = findProfileById(profiles, args.positional[2]);
        if (!index) {
            return directError(args, err, CliExitCode::NotFound, "source not found: " + std::string{args.positional[2]}, "NOT_FOUND", "profile-id", "existing source profile id", "lofibox source list");
        }
        const auto status = registry.credentials().status(profiles[*index]);
        printObject(out, format, credentialStatusFields(profiles[*index], status));
        const bool needs_secret = registry.sourceProfiles().readiness(profiles[*index]) == "NEEDS SECRET";
        return needs_secret ? static_cast<int>(CliExitCode::Credential) : 0;
    }

    if (verb == "capabilities") {
        if (args.positional.size() < 3U) {
            return directError(args, err, CliExitCode::Usage, "source capabilities requires a source id.", "INVALID_ARGUMENT", "profile-id", "source profile id", "lofibox source capabilities <profile-id>");
        }
        const auto index = findProfileById(profiles, args.positional[2]);
        if (!index) {
            return directError(args, err, CliExitCode::NotFound, "source not found: " + std::string{args.positional[2]}, "NOT_FOUND", "profile-id", "existing source profile id", "lofibox source list");
        }
        const auto& profile = profiles[*index];
        printObject(out, format, {
            {"id", profile.id},
            {"kind", app::remoteServerKindToString(profile.kind)},
            {"display_name", registry.sourceProfiles().kindDisplayName(profile.kind)},
            {"credentials", registry.sourceProfiles().supportsCredentials(profile.kind) ? "supported" : "none"},
            {"permission", registry.sourceProfiles().permissionLabel(profile.kind)},
            {"keeps_local_facts", boolLabel(registry.sourceProfiles().keepsLocalFacts(profile.kind))},
            {"verify_peer", boolLabel(profile.tls_policy.verify_peer)},
            {"allow_self_signed", boolLabel(profile.tls_policy.allow_self_signed)},
        });
        return 0;
    }

    return directError(args, err, CliExitCode::Usage, "unknown source command: " + std::string{verb}, "UNKNOWN_COMMAND", "verb", "source subcommand", "lofibox source --help");
}

int runCredentialsCommand(const ParsedArgs& args, application::AppServiceRegistry& registry, std::ostream& out, std::ostream& err)
{
    if (args.positional.size() < 2U || args.positional[1] == "help") {
        printDirectHelp(out);
        return 0;
    }
    const auto format = outputOptions(args);
    auto profiles = registry.sourceProfiles().loadProfiles();
    const auto verb = args.positional[1];

    if (verb == "list") {
        std::vector<CliFields> rows{};
        rows.reserve(profiles.size());
        for (const auto& profile : profiles) {
            rows.push_back(credentialStatusFields(profile, registry.credentials().status(profile)));
        }
        printPorcelainRows(out, format, rows, "NO CREDENTIAL REFS");
        return 0;
    }

    if (args.positional.size() < 3U) {
        return directError(args, err, CliExitCode::Usage, "credentials " + std::string{verb} + " requires a source id or credential ref.", "INVALID_ARGUMENT", "profile-id-or-ref", "source profile id or credential ref", "lofibox credentials " + std::string{verb} + " <profile-id-or-ref>");
    }

    const auto index = findProfileByIdOrCredentialRef(profiles, args.positional[2]);
    if (!index) {
        return directError(args, err, CliExitCode::NotFound, "credential ref not found: " + std::string{args.positional[2]}, "NOT_FOUND", "profile-id-or-ref", "existing source profile id or credential ref", "lofibox credentials list");
    }
    auto& profile = profiles[*index];

    if (verb == "status" || verb == "show-ref" || verb == "validate") {
        const auto status = registry.credentials().status(profile);
        printObject(out, format, credentialStatusFields(profile, status));
        if (verb == "validate" && registry.sourceProfiles().readiness(profile) == "NEEDS SECRET") {
            return static_cast<int>(CliExitCode::Credential);
        }
        return 0;
    }
    if (verb == "set") {
        auto patch = credentialPatchFromOptions(args);
        if (!hasCredentialPatch(patch)) {
            return directError(args, err, CliExitCode::Usage, "credentials set requires --username, --password-stdin, or --token-stdin.", "INVALID_ARGUMENT", "credential secret", "stdin secret input or username", "lofibox credentials set <profile-id-or-ref> --password-stdin");
        }
        const auto result = registry.credentials().setSecret(profile, patch);
        if (!result.accepted) {
            err << result.summary << '\n';
            return static_cast<int>(CliExitCode::Credential);
        }
        if (!registry.sourceProfiles().persistProfiles(profiles)) {
            return directError(args, err, CliExitCode::Persistence, "failed to save credential reference.", "PERSISTENCE_FAILED");
        }
        printObject(out, format, {
            {"status", "SAVED"},
            {"profile_id", profile.id},
            {"credential_ref", profile.credential_ref.id},
        });
        return 0;
    }
    if (verb == "delete") {
        const auto result = registry.credentials().deleteSecret(profile);
        if (!result.accepted) {
            err << result.summary << '\n';
            return static_cast<int>(CliExitCode::Credential);
        }
        if (!registry.sourceProfiles().persistProfiles(profiles)) {
            return directError(args, err, CliExitCode::Persistence, "failed to save credential reference.", "PERSISTENCE_FAILED");
        }
        printObject(out, format, {
            {"status", "DELETED"},
            {"profile_id", profile.id},
            {"credential_ref", profile.credential_ref.id},
        });
        return 0;
    }

    return directError(args, err, CliExitCode::Usage, "unknown credentials command: " + std::string{verb}, "UNKNOWN_COMMAND", "verb", "credentials subcommand", "lofibox credentials --help");
}

int runLibraryCommand(
    const ParsedArgs& args,
    application::AppServiceRegistry& registry,
    std::ostream& out,
    std::ostream& err)
{
    if (args.positional.size() < 2U || args.positional[1] == "help") {
        printDirectHelp(out);
        return 0;
    }
    const auto format = outputOptions(args);
    const auto verb = args.positional[1];
    auto refreshForRead = [&]() {
        const auto roots = rootsFromOption(args);
        return roots.empty()
            ? registry.libraryMutations().refreshConfiguredLibrary()
            : registry.libraryMutations().refreshLibrary(roots);
    };
    auto printStatus = [&]() {
        const auto& model = registry.libraryQueries().model();
        printObject(out, format, {
            {"state", stateLabel(registry.libraryQueries().state())},
            {"tracks", std::to_string(model.tracks.size())},
            {"albums", std::to_string(model.albums.size())},
            {"artists", std::to_string(model.artists.size())},
            {"genres", std::to_string(model.genres.size())},
            {"composers", std::to_string(model.composers.size())},
            {"compilations", std::to_string(model.compilations.size())},
            {"dirty", "false"},
            {"migration_needed", "false"},
            {"degraded", boolLabel(model.degraded || registry.libraryQueries().state() == app::LibraryIndexState::Degraded)},
        });
    };

    if (verb == "root") {
        return runLocalRootCommand(args, registry, out, err, 2U, "lofibox library root");
    }

    if (verb == "scan") {
        std::vector<std::filesystem::path> roots{};
        for (std::size_t index = 2; index < args.positional.size(); ++index) {
            roots.emplace_back(std::string{args.positional[index]});
        }
        for (const auto& root : rootsFromOption(args)) {
            roots.push_back(root);
        }
        const bool refreshed = roots.empty()
            ? registry.libraryMutations().refreshConfiguredLibrary()
            : registry.libraryMutations().refreshLibrary(roots);
        if (!refreshed) {
            err << "library scan unavailable.\n";
            return static_cast<int>(CliExitCode::Persistence);
        }
        printStatus();
        return registry.libraryQueries().state() == app::LibraryIndexState::Degraded ? 1 : 0;
    }

    if (verb == "status") {
        if (!refreshForRead()) {
            err << "library scan unavailable.\n";
            return static_cast<int>(CliExitCode::Persistence);
        }
        printStatus();
        return registry.libraryQueries().state() == app::LibraryIndexState::Degraded ? 1 : 0;
    }

    if (verb == "list") {
        if (args.positional.size() < 3U) {
            err << "library list requires tracks, albums, artists, genres, composers, or compilations.\n";
            return static_cast<int>(CliExitCode::Usage);
        }
        if (!refreshForRead()) {
            err << "library scan unavailable.\n";
            return static_cast<int>(CliExitCode::Persistence);
        }
        const auto& model = registry.libraryQueries().model();
        std::vector<CliFields> rows{};
        const auto noun = args.positional[2];
        if (noun == "tracks") {
            for (const auto& track : model.tracks) rows.push_back(trackFields(track));
        } else if (noun == "albums") {
            for (const auto& album : model.albums) {
                rows.push_back({{"album", album.album}, {"artist", album.artist}, {"tracks", std::to_string(album.track_ids.size())}});
            }
        } else if (noun == "artists") {
            for (const auto& artist : model.artists) rows.push_back({{"name", artist}});
        } else if (noun == "genres") {
            for (const auto& genre : model.genres) rows.push_back({{"name", genre}});
        } else if (noun == "composers") {
            for (const auto& composer : model.composers) rows.push_back({{"name", composer}});
        } else if (noun == "compilations") {
            for (const auto& compilation : model.compilations) {
                rows.push_back({{"album", compilation.album}, {"tracks", std::to_string(compilation.track_ids.size())}});
            }
        } else {
            err << "unknown library list noun: " << noun << '\n';
            return static_cast<int>(CliExitCode::Usage);
        }
        printPorcelainRows(out, format, rows, "NO RESULTS");
        return 0;
    }

    if (verb == "query") {
        if (args.positional.size() < 3U) {
            err << "library query requires text.\n";
            return static_cast<int>(CliExitCode::Usage);
        }
        const auto noun = args.positional[2];
        const bool structured = noun == "tracks" || noun == "albums";
        if (structured) {
            if (!refreshForRead()) {
                err << "library scan unavailable.\n";
                return static_cast<int>(CliExitCode::Persistence);
            }
            const auto& model = registry.libraryQueries().model();
            std::vector<CliFields> rows{};
            if (noun == "tracks") {
                std::vector<const app::TrackRecord*> tracks{};
                for (const auto& track : model.tracks) {
                    if (const auto artist = optionValue(args, "artist"); artist && track.artist != *artist) continue;
                    if (const auto genre = optionValue(args, "genre"); genre && track.genre != *genre) continue;
                    if (const auto album = optionValueAny(args, {"album-id", "album"}); album && track.album != *album) continue;
                    tracks.push_back(&track);
                }
                if (optionPresent(args, "most-played")) {
                    std::sort(tracks.begin(), tracks.end(), [](const app::TrackRecord* left, const app::TrackRecord* right) {
                        return left->play_count > right->play_count;
                    });
                } else if (optionPresent(args, "recently-played")) {
                    std::sort(tracks.begin(), tracks.end(), [](const app::TrackRecord* left, const app::TrackRecord* right) {
                        return left->last_played > right->last_played;
                    });
                } else if (optionPresent(args, "recently-added")) {
                    std::sort(tracks.begin(), tracks.end(), [](const app::TrackRecord* left, const app::TrackRecord* right) {
                        return left->added_time > right->added_time;
                    });
                }
                for (const auto* track : tracks) {
                    rows.push_back(trackFields(*track));
                }
            } else {
                for (const auto& album : model.albums) {
                    if (const auto artist = optionValue(args, "artist"); artist && album.artist != *artist) continue;
                    if (const auto genre = optionValue(args, "genre")) {
                        bool has_genre = false;
                        for (const auto id : album.track_ids) {
                            const auto* track = registry.libraryQueries().findTrack(id);
                            has_genre = has_genre || (track != nullptr && track->genre == *genre);
                        }
                        if (!has_genre) continue;
                    }
                    rows.push_back({{"album", album.album}, {"artist", album.artist}, {"tracks", std::to_string(album.track_ids.size())}});
                }
            }
            printPorcelainRows(out, format, rows, "NO RESULTS");
            return 0;
        }

        std::string query{};
        for (std::size_t index = 2; index < args.positional.size(); ++index) {
            if (!query.empty()) {
                query.push_back(' ');
            }
            query.append(args.positional[index]);
        }
        if (!refreshForRead()) {
            err << "library scan unavailable.\n";
            return static_cast<int>(CliExitCode::Persistence);
        }
        const auto items = registry.libraryQueries().searchLocal(query, 20);
        std::vector<CliFields> rows{};
        for (const auto& item : items) {
            rows.push_back(mediaItemFields(item, "local"));
        }
        printPorcelainRows(out, format, rows, "NO RESULTS");
        return 0;
    }

    err << "unknown library command: " << verb << '\n';
    return static_cast<int>(CliExitCode::Usage);
}

int runCacheCommand(const ParsedArgs& args, application::AppServiceRegistry& registry, std::ostream& out, std::ostream& err)
{
    if (args.positional.size() < 2U || args.positional[1] == "help") {
        printDirectHelp(out);
        return 0;
    }
    const auto format = outputOptions(args);
    if (args.positional[1] == "status") {
        const auto result = registry.cacheCommands().status();
        if (!result.command.accepted) {
            err << result.command.summary << '\n';
            return static_cast<int>(CliExitCode::Persistence);
        }
        printObject(out, format, cacheStatusFields(result));
        return 0;
    }
    if (args.positional[1] == "clear" || args.positional[1] == "purge") {
        const auto result = registry.cacheCommands().clearAll();
        if (!result.command.accepted) {
            err << result.command.summary << '\n';
            return static_cast<int>(CliExitCode::Persistence);
        }
        printObject(out, format, {
            {"status", "CLEARED"},
            {"removed_entries", std::to_string(result.removed_entries)},
            {"before_entries", std::to_string(result.before.entry_count)},
            {"after_entries", std::to_string(result.after.entry_count)},
        });
        return 0;
    }
    if (args.positional[1] == "gc") {
        const auto result = registry.cacheCommands().collectGarbage();
        if (!result.command.accepted) {
            err << result.command.summary << '\n';
            return static_cast<int>(CliExitCode::Persistence);
        }
        printObject(out, format, {
            {"status", "GC"},
            {"removed_entries", std::to_string(result.removed_entries)},
            {"before_entries", std::to_string(result.before.entry_count)},
            {"after_entries", std::to_string(result.after.entry_count)},
        });
        return 0;
    }
    err << "unknown cache command: " << args.positional[1] << '\n';
    return static_cast<int>(CliExitCode::Usage);
}

int runDoctorCommand(const ParsedArgs& args, application::AppServiceRegistry& registry, std::ostream& out)
{
    const auto format = outputOptions(args);
    const auto profiles = registry.sourceProfiles().loadProfiles();
    const auto snapshot = registry.diagnostics().snapshot(profiles);
    if (format.quiet) {
        return snapshot.degraded ? 1 : 0;
    }
    if (format.json) {
        out << "{\"status\":";
        printJsonString(out, snapshot.command.summary);
        out << ",\"degraded\":" << (snapshot.degraded ? "true" : "false") << ",\"capabilities\":[";
        bool first = true;
        for (const auto& capability : snapshot.capabilities) {
            if (!first) out << ',';
            first = false;
            out << "{\"name\":";
            printJsonString(out, capability.name);
            out << ",\"available\":" << (capability.available ? "true" : "false") << ",\"detail\":";
            printJsonString(out, capability.detail);
            out << '}';
        }
        out << "],\"sources\":[";
        first = true;
        for (const auto& source : snapshot.sources) {
            if (!first) out << ',';
            first = false;
            out << "{\"label\":";
            printJsonString(out, source.source_label);
            out << ",\"connection\":";
            printJsonString(out, source.connection_status);
            out << ",\"permission\":";
            printJsonString(out, source.permission);
            out << '}';
        }
        out << "]}\n";
        return snapshot.degraded ? 1 : 0;
    }
    if (format.porcelain) {
        out << "STATUS\t" << snapshot.command.summary << '\n';
    } else {
        out << "Status: " << snapshot.command.summary << '\n';
    }
    for (const auto& capability : snapshot.capabilities) {
        if (format.porcelain) {
            out << "CAPABILITY\t" << capability.name << '\t'
                << (capability.available ? "OK" : "MISSING") << '\t'
                << capability.detail << '\n';
        } else {
            out << "Capability " << capability.name << ": "
                << (capability.available ? "OK" : "MISSING")
                << " (" << capability.detail << ")\n";
        }
    }
    for (const auto& source : snapshot.sources) {
        if (format.porcelain) {
            out << "SOURCE\t" << source.source_label << '\t'
                << source.connection_status << '\t'
                << source.permission << '\n';
        } else {
            out << "Source " << source.source_label << ": "
                << source.connection_status << " / " << source.permission << '\n';
        }
    }
    return snapshot.degraded ? 1 : 0;
}

int runSearchCommand(const ParsedArgs& args, application::AppServiceRegistry& registry, std::ostream& out, std::ostream& err)
{
    if (args.positional.size() < 2U || args.positional[1] == "help") {
        printDirectHelp(out);
        return 0;
    }
    const auto format = outputOptions(args);
    std::string query{};
    for (std::size_t index = 1; index < args.positional.size(); ++index) {
        if (!query.empty()) query.push_back(' ');
        query.append(args.positional[index]);
    }
    const auto roots = rootsFromOption(args);
    const bool refreshed = roots.empty()
        ? registry.libraryMutations().refreshConfiguredLibrary()
        : registry.libraryMutations().refreshLibrary(roots);
    if (!refreshed) {
        err << "library scan unavailable.\n";
        return static_cast<int>(CliExitCode::Persistence);
    }

    std::vector<CliFields> rows{};
    if (!optionPresent(args, "source")) {
        for (const auto& item : registry.libraryQueries().searchLocal(query, 20)) {
            rows.push_back(mediaItemFields(item, "local"));
        }
    }

    if (!optionPresent(args, "local")) {
        auto profiles = registry.sourceProfiles().loadProfiles();
        std::vector<std::size_t> profile_indexes{};
        if (const auto source = optionValue(args, "source")) {
            const auto index = findProfileById(profiles, *source);
            if (!index) {
                err << "source not found: " << *source << '\n';
                return static_cast<int>(CliExitCode::NotFound);
            }
            profile_indexes.push_back(*index);
        } else if (optionPresent(args, "all")) {
            for (std::size_t index = 0; index < profiles.size(); ++index) {
                profile_indexes.push_back(index);
            }
        }

        for (const auto index : profile_indexes) {
            if (profiles[index].kind == app::RemoteServerKind::LocalRoot) {
                continue;
            }
            auto result = registry.remoteBrowseQueries().search(profiles[index], profiles.size(), query, 20);
            if (!result.command.accepted && optionPresent(args, "source")) {
                err << result.command.summary << '\n';
                return static_cast<int>(CliExitCode::Network);
            }
            for (const auto& track : result.tracks) {
                rows.push_back(remoteTrackFields(track, result.source_label));
            }
        }
    }

    printPorcelainRows(out, format, rows, "NO RESULTS");
    return 0;
}

int runRemoteCommand(const ParsedArgs& args, application::AppServiceRegistry& registry, std::ostream& out, std::ostream& err)
{
    if (args.positional.size() < 3U || args.positional[1] == "help") {
        printDirectHelp(out);
        return 0;
    }
    const auto format = outputOptions(args);
    auto profiles = registry.sourceProfiles().loadProfiles();
    const auto verb = args.positional[1];
    const auto index = findProfileById(profiles, args.positional[2]);
    if (!index) {
        err << "source not found: " << args.positional[2] << '\n';
        return static_cast<int>(CliExitCode::NotFound);
    }
    auto& profile = profiles[*index];

    if (verb == "browse") {
        application::RemoteBrowseQueryResult result{};
        if (args.positional.size() < 4U) {
            result = registry.remoteBrowseQueries().browseRoot(profile, profiles.size(), 50);
        } else {
            const auto parent = rootNodeForPath(args.positional[3]);
            if (!parent) {
                err << "remote browse path is not a known root category: " << args.positional[3] << '\n';
                return static_cast<int>(CliExitCode::NotFound);
            }
            if (parent->kind == app::RemoteCatalogNodeKind::Root) {
                result = registry.remoteBrowseQueries().browseRoot(profile, profiles.size(), 50);
            } else {
                const auto probe = registry.sourceProfiles().probe(profile, profiles.size());
                result = registry.remoteBrowseQueries().browseChild(profile, probe.session, *parent, 50);
            }
        }
        std::vector<CliFields> rows{};
        for (const auto& node : result.nodes) {
            rows.push_back(remoteNodeFields(node));
        }
        printPorcelainRows(out, format, rows, "NO REMOTE ITEMS");
        return result.command.accepted ? 0 : static_cast<int>(CliExitCode::Network);
    }

    if (verb == "recent") {
        const auto parent = rootNodeForPath("/recently-played");
        if (!parent) {
            err << "remote recent root category unavailable.\n";
            return static_cast<int>(CliExitCode::NotFound);
        }
        const auto probe = registry.sourceProfiles().probe(profile, profiles.size());
        const auto result = registry.remoteBrowseQueries().browseChild(profile, probe.session, *parent, 50);
        std::vector<CliFields> rows{};
        for (const auto& node : result.nodes) {
            rows.push_back(remoteNodeFields(node));
        }
        printPorcelainRows(out, format, rows, "NO RECENT ITEMS");
        return result.command.accepted ? 0 : static_cast<int>(CliExitCode::Network);
    }

    if (verb == "search") {
        if (args.positional.size() < 4U) {
            err << "remote search requires text.\n";
            return static_cast<int>(CliExitCode::Usage);
        }
        std::string query{};
        for (std::size_t query_index = 3; query_index < args.positional.size(); ++query_index) {
            if (!query.empty()) query.push_back(' ');
            query.append(args.positional[query_index]);
        }
        const auto result = registry.remoteBrowseQueries().search(profile, profiles.size(), query, 50);
        if (!result.command.accepted) {
            err << result.command.summary << '\n';
            return static_cast<int>(CliExitCode::Network);
        }
        std::vector<CliFields> rows{};
        for (const auto& track : result.tracks) {
            rows.push_back(remoteTrackFields(track, result.source_label));
        }
        printPorcelainRows(out, format, rows, "NO RESULTS");
        return 0;
    }

    if (verb == "resolve" || verb == "stream-info") {
        if (args.positional.size() < 4U) {
            err << "remote " << verb << " requires an item id.\n";
            return static_cast<int>(CliExitCode::Usage);
        }
        const auto probe = registry.sourceProfiles().probe(profile, profiles.size());
        app::RemoteTrack track{};
        track.id = std::string{args.positional[3]};
        track.title = track.id;
        track.source_id = profile.id;
        track.source_label = registry.sourceProfiles().profileLabel(profile);
        const auto result = registry.remoteBrowseQueries().resolve(profile, probe.session, track);
        if (verb == "resolve") {
            printObject(out, format, {
                {"profile_id", profile.id},
                {"item_id", track.id},
                {"resolved", boolLabel(result.stream.has_value())},
                {"url", result.stream ? result.stream->diagnostics.resolved_url_redacted : "-"},
                {"seekable", result.stream ? boolLabel(result.stream->seekable) : "false"},
            });
        } else {
            const auto diagnostics = registry.remoteBrowseQueries().streamDiagnostics(profile, result.stream);
            printObject(out, format, {
                {"profile_id", profile.id},
                {"item_id", track.id},
                {"resolved", boolLabel(diagnostics.resolved)},
                {"source", diagnostics.source_name},
                {"url", diagnostics.redacted_url.empty() ? "-" : diagnostics.redacted_url},
                {"connection", diagnostics.connection_status},
                {"buffer", diagnostics.buffer_state},
                {"recovery", diagnostics.recovery_action},
                {"bitrate", std::to_string(diagnostics.bitrate_kbps)},
                {"codec", diagnostics.codec.empty() ? "-" : diagnostics.codec},
                {"sample_rate", std::to_string(diagnostics.sample_rate_hz)},
                {"channels", std::to_string(diagnostics.channel_count)},
                {"live", boolLabel(diagnostics.live)},
                {"seekable", boolLabel(diagnostics.seekable)},
            });
        }
        return result.stream ? 0 : static_cast<int>(CliExitCode::Network);
    }

    err << "unknown remote command: " << verb << '\n';
    return static_cast<int>(CliExitCode::Usage);
}

} // namespace

std::optional<int> runDirectCliCommand(
    int argc,
    char** argv,
    app::RuntimeServices& services,
    std::ostream& out,
    std::ostream& err)
{
    if (argc < 2) {
        return std::nullopt;
    }
    auto args = parseArgs(argc, argv);
    if (args.positional.empty()) {
        return std::nullopt;
    }
    const std::string_view command{args.positional.front()};
    if (!isDirectCommand(command)) {
        return std::nullopt;
    }
    if (helpRequested(args)) {
        printDirectHelp(out);
        return 0;
    }

    application::AppServiceHost service_host{services};
    auto registry = service_host.registry();

    if (command == "source") {
        return runSourceCommand(args, registry, out, err);
    }
    if (command == "credentials") {
        return runCredentialsCommand(args, registry, out, err);
    }
    if (command == "cache") {
        return runCacheCommand(args, registry, out, err);
    }
    if (command == "doctor") {
        return runDoctorCommand(args, registry, out);
    }
    if (command == "library") {
        return runLibraryCommand(args, registry, out, err);
    }
    if (command == "search") {
        return runSearchCommand(args, registry, out, err);
    }
    if (command == "remote") {
        return runRemoteCommand(args, registry, out, err);
    }
    if (command == "metadata") {
        return runMetadataCommand(args, registry, services, out, err);
    }
    if (command == "lyrics") {
        return runLyricsCommand(args, registry, services, out, err);
    }
    if (command == "artwork") {
        return runArtworkCommand(args, registry, services, out, err);
    }
    if (command == "fingerprint") {
        return runFingerprintCommand(args, registry, services, out, err);
    }
    return std::nullopt;
}

} // namespace lofibox::cli
