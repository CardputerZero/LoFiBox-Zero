// SPDX-License-Identifier: GPL-3.0-or-later

#include "application/source_profile_command_service.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstdint>
#include <filesystem>
#include <sstream>
#include <string>
#include <utility>

#include "app/remote_profile_store.h"
#include "remote/common/remote_provider_contract.h"
#include "remote/common/remote_source_registry.h"

namespace lofibox::application {
namespace {

std::string envValue(const char* name)
{
    if (const char* value = std::getenv(name); value != nullptr && *value != '\0') {
        return value;
    }
    return {};
}

std::filesystem::path homeDir()
{
    if (auto home = envValue("HOME"); !home.empty()) {
        return home;
    }
#if defined(_WIN32)
    if (auto home = envValue("USERPROFILE"); !home.empty()) {
        return home;
    }
#endif
    return {};
}

std::filesystem::path expandHomeToken(const std::filesystem::path& path)
{
    const auto text = path.string();
    if (text.size() >= 2U && text[0] == '~' && (text[1] == '/' || text[1] == '\\')) {
        if (const auto home = homeDir(); !home.empty()) {
            return home / text.substr(2);
        }
    }
    return path;
}

std::filesystem::path normalizedLocalRootPath(const std::filesystem::path& path)
{
    std::error_code ec{};
    auto expanded = expandHomeToken(path);
    if (expanded.is_relative()) {
        expanded = std::filesystem::absolute(expanded, ec);
        if (ec) {
            expanded = expandHomeToken(path);
        }
    }
    const auto canonical = std::filesystem::weakly_canonical(expanded, ec);
    return ec ? expanded.lexically_normal() : canonical;
}

std::string hexIdForPath(const std::filesystem::path& path)
{
    const auto text = normalizedLocalRootPath(path).string();
    std::uint64_t hash = 1469598103934665603ULL;
    for (const unsigned char ch : text) {
        hash ^= ch;
        hash *= 1099511628211ULL;
    }
    std::ostringstream out{};
    out << std::hex << hash;
    return "local-root-" + out.str();
}

bool isLocalRootProfile(const app::RemoteServerProfile& profile) noexcept
{
    return profile.kind == app::RemoteServerKind::LocalRoot;
}

} // namespace

SourceProfileCommandService::SourceProfileCommandService(const app::RuntimeServices& services) noexcept
    : services_(services)
{
}

std::vector<app::RemoteServerProfile> SourceProfileCommandService::loadProfiles() const
{
    return services_.remote.remote_profile_store->loadProfiles();
}

bool SourceProfileCommandService::persistProfiles(std::vector<app::RemoteServerProfile>& profiles) const
{
    for (auto& profile : profiles) {
        ensureCredentialRef(profile, profiles.size());
    }
    return services_.remote.remote_profile_store->saveProfiles(profiles);
}

app::RemoteServerProfile SourceProfileCommandService::createProfile(app::RemoteServerKind kind, std::size_t existing_profile_count) const
{
    app::RemoteServerProfile profile{};
    profile.kind = kind;
    profile.id = defaultProfileId(kind, existing_profile_count);
    profile.name = defaultProfileName(kind);
    profile.tls_policy.verify_peer = true;
    ensureCredentialRef(profile, existing_profile_count + 1U);
    return profile;
}

std::size_t SourceProfileCommandService::ensureProfileForKind(std::vector<app::RemoteServerProfile>& profiles, app::RemoteServerKind kind) const
{
    if (const auto existing = findProfileByKind(profiles, kind)) {
        return *existing;
    }
    profiles.push_back(createProfile(kind, profiles.size()));
    return profiles.size() - 1U;
}

std::optional<std::size_t> SourceProfileCommandService::findProfileByKind(const std::vector<app::RemoteServerProfile>& profiles, app::RemoteServerKind kind) const
{
    const auto it = std::find_if(profiles.begin(), profiles.end(), [kind](const app::RemoteServerProfile& profile) {
        return profile.kind == kind;
    });
    if (it == profiles.end()) {
        return std::nullopt;
    }
    return static_cast<std::size_t>(std::distance(profiles.begin(), it));
}

void SourceProfileCommandService::ensureCredentialRef(app::RemoteServerProfile& profile, std::size_t profile_count) const
{
    if (profile.id.empty()) {
        profile.id = defaultProfileId(profile.kind, profile_count);
    }
    if (!supportsCredentials(profile.kind)) {
        profile.credential_ref.id.clear();
        return;
    }
    if (profile.credential_ref.id.empty()) {
        profile.credential_ref.id = "credential/" + profile.id;
    }
}

bool SourceProfileCommandService::updateTextField(app::RemoteServerProfile& profile, SourceProfileTextField field, std::string_view value, std::size_t profile_count) const
{
    switch (field) {
    case SourceProfileTextField::Label:
        profile.name = std::string{value};
        return true;
    case SourceProfileTextField::Address:
        profile.base_url = std::string{value};
        if (isLocalRootProfile(profile)) {
            profile.local_root = std::string{normalizedLocalRootPath(profile.base_url).string()};
            profile.base_url = profile.local_root;
        }
        return true;
    case SourceProfileTextField::Username:
        profile.username = std::string{value};
        return true;
    case SourceProfileTextField::Password:
    case SourceProfileTextField::ApiToken:
        ensureCredentialRef(profile, profile_count);
        return false;
    }
    return false;
}

bool SourceProfileCommandService::toggleTlsVerify(app::RemoteServerProfile& profile, std::vector<app::RemoteServerProfile>& profiles) const
{
    profile.tls_policy.verify_peer = !profile.tls_policy.verify_peer;
    if (profile.tls_policy.verify_peer) {
        profile.tls_policy.allow_self_signed = false;
    }
    return persistProfiles(profiles);
}

bool SourceProfileCommandService::toggleSelfSigned(app::RemoteServerProfile& profile, std::vector<app::RemoteServerProfile>& profiles) const
{
    profile.tls_policy.allow_self_signed = !profile.tls_policy.allow_self_signed;
    if (profile.tls_policy.allow_self_signed) {
        profile.tls_policy.verify_peer = false;
    }
    return persistProfiles(profiles);
}

SourceProfileProbeResult SourceProfileCommandService::probe(app::RemoteServerProfile& profile, std::size_t profile_count) const
{
    ensureCredentialRef(profile, profile_count);
    if (isLocalRootProfile(profile)) {
        const auto path = localRootPath(profile);
        std::error_code ec{};
        const bool ok = !path.empty() && std::filesystem::exists(path, ec) && !ec && std::filesystem::is_directory(path, ec) && !ec;
        return {
            ok
                ? CommandResult::success("source-profile.local-root.online", "ONLINE")
                : CommandResult::failure("source-profile.local-root.unavailable", path.empty() ? "NEEDS PATH" : "UNAVAILABLE"),
            app::RemoteSourceSession{ok, profile.name, {}, {}, ok ? "OK" : "LOCAL ROOT UNAVAILABLE"}};
    }
    auto session = services_.remote.remote_source_provider->probe(profile);
    return {
        session.available
            ? CommandResult::success("source-profile.probe.online", "ONLINE")
            : CommandResult::failure("source-profile.probe.failed", session.message.empty() ? "FAILED" : session.message),
        std::move(session)};
}

std::vector<app::RemoteServerProfile> SourceProfileCommandService::listLocalRoots() const
{
    std::vector<app::RemoteServerProfile> roots{};
    for (auto profile : loadProfiles()) {
        if (!isLocalRootProfile(profile)) {
            continue;
        }
        if (profile.local_root.empty()) {
            profile.local_root = profile.base_url;
        }
        if (profile.base_url.empty()) {
            profile.base_url = profile.local_root;
        }
        roots.push_back(std::move(profile));
    }
    const bool has_enabled_root = std::any_of(roots.begin(), roots.end(), [](const auto& profile) {
        return profile.enabled;
    });
    if (!has_enabled_root) {
        auto fallback = createProfile(app::RemoteServerKind::LocalRoot, roots.size());
        fallback.id = "local-root-default";
        fallback.name = "Music";
        fallback.local_root = defaultLocalRoot().string();
        fallback.base_url = fallback.local_root;
        fallback.default_eligible = true;
        fallback.enabled = true;
        fallback.credential_ref.id.clear();
        roots.push_back(std::move(fallback));
    }
    return roots;
}

std::vector<std::filesystem::path> SourceProfileCommandService::enabledLocalRoots() const
{
    std::vector<std::filesystem::path> roots{};
    for (const auto& profile : loadProfiles()) {
        if (!isLocalRootProfile(profile) || !profile.enabled) {
            continue;
        }
        const auto path = localRootPath(profile);
        if (!path.empty()) {
            roots.push_back(path);
        }
    }
    if (roots.empty()) {
        roots.push_back(defaultLocalRoot());
    }
    return roots;
}

LocalRootCommandResult SourceProfileCommandService::addLocalRoot(const std::filesystem::path& path, std::optional<std::string> name) const
{
    auto profiles = loadProfiles();
    const auto normalized = normalizedLocalRootPath(path);
    for (const auto& existing : profiles) {
        if (!isLocalRootProfile(existing)) {
            continue;
        }
        if (normalizedLocalRootPath(localRootPath(existing)) == normalized) {
            return {CommandResult::failure("source-profile.local-root.exists", "LOCAL ROOT EXISTS"), existing};
        }
    }

    auto profile = createProfile(app::RemoteServerKind::LocalRoot, profiles.size());
    profile.id = hexIdForPath(normalized);
    profile.name = name && !name->empty() ? *name : normalized.filename().string();
    if (profile.name.empty()) {
        profile.name = "Music";
    }
    profile.local_root = normalized.string();
    profile.base_url = profile.local_root;
    profile.default_eligible = true;
    profile.enabled = true;
    profile.credential_ref.id.clear();
    profiles.push_back(profile);
    if (!persistProfiles(profiles)) {
        return {CommandResult::failure("source-profile.local-root.persist-failed", "failed to save local root"), profile};
    }
    return {CommandResult::success("source-profile.local-root.added", "ADDED"), profile};
}

LocalRootCommandResult SourceProfileCommandService::removeLocalRoot(std::string_view id_or_path) const
{
    auto profiles = loadProfiles();
    const std::string target_id{id_or_path};
    const std::filesystem::path target_path{target_id};
    const auto normalized_target = normalizedLocalRootPath(target_path);
    const auto it = std::find_if(profiles.begin(), profiles.end(), [&](const auto& profile) {
        return isLocalRootProfile(profile)
            && (profile.id == target_id || normalizedLocalRootPath(localRootPath(profile)) == normalized_target);
    });
    if (it == profiles.end()) {
        return {CommandResult::failure("source-profile.local-root.not-found", "LOCAL ROOT NOT FOUND"), {}};
    }
    auto removed = *it;
    profiles.erase(it);
    if (!persistProfiles(profiles)) {
        return {CommandResult::failure("source-profile.local-root.persist-failed", "failed to save local root"), removed};
    }
    return {CommandResult::success("source-profile.local-root.removed", "REMOVED"), removed};
}

LocalRootCommandResult SourceProfileCommandService::enableLocalRoot(std::string_view id_or_path) const
{
    auto profiles = loadProfiles();
    const std::string target_id{id_or_path};
    const auto normalized_target = normalizedLocalRootPath(std::filesystem::path{target_id});
    auto it = std::find_if(profiles.begin(), profiles.end(), [&](const auto& profile) {
        return isLocalRootProfile(profile)
            && (profile.id == target_id || normalizedLocalRootPath(localRootPath(profile)) == normalized_target);
    });
    if (it == profiles.end()) {
        return {CommandResult::failure("source-profile.local-root.not-found", "LOCAL ROOT NOT FOUND"), {}};
    }
    it->enabled = true;
    if (!persistProfiles(profiles)) {
        return {CommandResult::failure("source-profile.local-root.persist-failed", "failed to save local root"), *it};
    }
    return {CommandResult::success("source-profile.local-root.enabled", "ENABLED"), *it};
}

LocalRootCommandResult SourceProfileCommandService::disableLocalRoot(std::string_view id_or_path) const
{
    auto profiles = loadProfiles();
    const std::string target_id{id_or_path};
    const auto normalized_target = normalizedLocalRootPath(std::filesystem::path{target_id});
    auto it = std::find_if(profiles.begin(), profiles.end(), [&](const auto& profile) {
        return isLocalRootProfile(profile)
            && (profile.id == target_id || normalizedLocalRootPath(localRootPath(profile)) == normalized_target);
    });
    if (it == profiles.end()) {
        return {CommandResult::failure("source-profile.local-root.not-found", "LOCAL ROOT NOT FOUND"), {}};
    }
    it->enabled = false;
    if (!persistProfiles(profiles)) {
        return {CommandResult::failure("source-profile.local-root.persist-failed", "failed to save local root"), *it};
    }
    return {CommandResult::success("source-profile.local-root.disabled", "DISABLED"), *it};
}

std::string SourceProfileCommandService::kindDisplayName(app::RemoteServerKind kind) const
{
    const auto manifest = remote::remoteProviderManifest(kind);
    return manifest.display_name.empty() ? app::remoteServerKindToString(kind) : manifest.display_name;
}

std::string SourceProfileCommandService::defaultProfileId(app::RemoteServerKind kind, std::size_t index) const
{
    return app::remoteServerKindToString(kind) + "-" + std::to_string(static_cast<int>(index + 1U));
}

std::string SourceProfileCommandService::defaultProfileName(app::RemoteServerKind kind) const
{
    auto name = kindDisplayName(kind);
    std::transform(name.begin(), name.end(), name.begin(), [](unsigned char ch) {
        return static_cast<char>(std::toupper(ch));
    });
    return name;
}

std::string SourceProfileCommandService::profileLabel(const app::RemoteServerProfile& profile) const
{
    if (!profile.name.empty()) {
        return profile.name;
    }
    if (!profile.base_url.empty()) {
        return profile.base_url;
    }
    return defaultProfileName(profile.kind);
}

SourceProfileCredentialMode SourceProfileCommandService::credentialMode(app::RemoteServerKind kind) const noexcept
{
    switch (kind) {
    case app::RemoteServerKind::Jellyfin:
    case app::RemoteServerKind::OpenSubsonic:
    case app::RemoteServerKind::Navidrome:
    case app::RemoteServerKind::Emby:
        return SourceProfileCredentialMode::Required;
    case app::RemoteServerKind::PlaylistManifest:
    case app::RemoteServerKind::WebDav:
    case app::RemoteServerKind::Ftp:
    case app::RemoteServerKind::Sftp:
        return SourceProfileCredentialMode::Optional;
    case app::RemoteServerKind::DirectUrl:
    case app::RemoteServerKind::LocalRoot:
    case app::RemoteServerKind::InternetRadio:
    case app::RemoteServerKind::Hls:
    case app::RemoteServerKind::Dash:
    case app::RemoteServerKind::Smb:
    case app::RemoteServerKind::Nfs:
    case app::RemoteServerKind::DlnaUpnp:
        return SourceProfileCredentialMode::None;
    }
    return SourceProfileCredentialMode::None;
}

bool SourceProfileCommandService::supportsCredentials(app::RemoteServerKind kind) const noexcept
{
    return credentialMode(kind) != SourceProfileCredentialMode::None;
}

std::string SourceProfileCommandService::readiness(const app::RemoteServerProfile& profile) const
{
    if (isLocalRootProfile(profile)) {
        if (!profile.enabled) {
            return "DISABLED";
        }
        return localRootPath(profile).empty() ? "NEEDS PATH" : "READY";
    }
    if (profile.base_url.empty()) {
        return "NEEDS URL";
    }
    if (credentialMode(profile.kind) != SourceProfileCredentialMode::Required) {
        return "READY";
    }
    if (profile.username.empty()) {
        return "NEEDS USER";
    }
    if (profile.password.empty() && profile.api_token.empty()) {
        return "NEEDS SECRET";
    }
    return "READY";
}

std::string SourceProfileCommandService::credentialValueLabel(app::RemoteServerKind kind, std::string_view value) const
{
    const auto mode = credentialMode(kind);
    if (mode == SourceProfileCredentialMode::None) {
        return "N/A";
    }
    if (value.empty() && mode == SourceProfileCredentialMode::Optional) {
        return "OPTIONAL";
    }
    return value.empty() ? "EMPTY" : "SET";
}

std::string SourceProfileCommandService::usernameLabel(const app::RemoteServerProfile& profile) const
{
    const auto mode = credentialMode(profile.kind);
    if (mode == SourceProfileCredentialMode::None) {
        return "N/A";
    }
    if (profile.username.empty()) {
        return mode == SourceProfileCredentialMode::Optional ? "OPTIONAL" : "MISSING";
    }
    return profile.username;
}

std::string SourceProfileCommandService::permissionLabel(app::RemoteServerKind kind) const
{
    if (kind == app::RemoteServerKind::LocalRoot) {
        return "READ ONLY";
    }
    const auto manifest = remote::remoteProviderManifest(kind);
    if (remote::remoteProviderHasCapability(manifest, remote::RemoteProviderCapability::WritableMetadata)
        || remote::remoteProviderHasCapability(manifest, remote::RemoteProviderCapability::WritableFavorites)) {
        return "READ/WRITE";
    }
    return "READ ONLY";
}

bool SourceProfileCommandService::keepsLocalFacts(app::RemoteServerKind kind) const
{
    if (kind == app::RemoteServerKind::LocalRoot) {
        return true;
    }
    const auto manifest = remote::remoteProviderManifest(kind);
    const bool writable_metadata =
        remote::remoteProviderHasCapability(manifest, remote::RemoteProviderCapability::WritableMetadata)
        && !remote::remoteProviderHasCapability(manifest, remote::RemoteProviderCapability::ReadOnly);
    return !writable_metadata;
}

std::filesystem::path SourceProfileCommandService::defaultLocalRoot() const
{
    if (const auto home = homeDir(); !home.empty()) {
        return home / "Music";
    }
#if defined(_WIN32)
    return std::filesystem::temp_directory_path() / "Music";
#else
    return "/music";
#endif
}

std::filesystem::path SourceProfileCommandService::localRootPath(const app::RemoteServerProfile& profile) const
{
    const auto raw = profile.local_root.empty() ? profile.base_url : profile.local_root;
    return raw.empty() ? std::filesystem::path{} : normalizedLocalRootPath(raw);
}

} // namespace lofibox::application
