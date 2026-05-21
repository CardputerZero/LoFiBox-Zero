// SPDX-License-Identifier: GPL-3.0-or-later

#include "platform/host/xdg_remote_profile_store.h"

#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <string>

#include "platform/host/runtime_host_internal.h"
#include "platform/host/runtime_paths.h"

namespace fs = std::filesystem;

namespace lofibox::platform::host {
namespace {

std::string field(const app::RemoteServerProfile& profile, std::string_view key)
{
    if (key == "kind") return app::remoteServerKindToString(profile.kind);
    if (key == "id") return profile.id;
    if (key == "name") return profile.name;
    if (key == "base_url") return profile.base_url;
    if (key == "local_root") return profile.local_root;
    if (key == "username") return profile.kind == app::RemoteServerKind::LocalRoot ? std::string{} : profile.username;
    if (key == "credential_ref") return profile.kind == app::RemoteServerKind::LocalRoot ? std::string{} : profile.credential_ref.id;
    if (key == "verify_peer") return profile.tls_policy.verify_peer ? "true" : "false";
    if (key == "allow_self_signed") return profile.tls_policy.allow_self_signed ? "true" : "false";
    if (key == "default_eligible") return profile.default_eligible ? "true" : "false";
    if (key == "enabled") return profile.enabled ? "true" : "false";
    return {};
}

std::string lineForProfile(const app::RemoteServerProfile& profile)
{
    const auto safe = app::sanitizeRemoteProfileForPersistence(profile);
    std::ostringstream out;
    const char separator = '\t';
    out << runtime_detail::jsonEscape(field(safe, "kind")) << separator
        << runtime_detail::jsonEscape(field(safe, "id")) << separator
        << runtime_detail::jsonEscape(field(safe, "name")) << separator
        << runtime_detail::jsonEscape(field(safe, "base_url")) << separator
        << runtime_detail::jsonEscape(field(safe, "username")) << separator
        << runtime_detail::jsonEscape(field(safe, "credential_ref")) << separator
        << field(safe, "verify_peer") << separator
        << field(safe, "allow_self_signed") << separator
        << runtime_detail::jsonEscape(field(safe, "local_root")) << separator
        << field(safe, "default_eligible") << separator
        << field(safe, "enabled");
    return out.str();
}

std::vector<std::string> splitTabLine(const std::string& line)
{
    std::vector<std::string> values{};
    std::size_t begin = 0;
    while (begin <= line.size()) {
        const auto end = line.find('\t', begin);
        values.push_back(end == std::string::npos ? line.substr(begin) : line.substr(begin, end - begin));
        if (end == std::string::npos) {
            break;
        }
        begin = end + 1;
    }
    return values;
}

std::string unescapeStoredValue(const std::string& value)
{
    std::string quoted = "\"" + value + "\"";
    if (auto parsed = runtime_detail::parseJsonStringAt(quoted, 0)) {
        return *parsed;
    }
    return value;
}

struct RemoteCredentialSecret {
    std::string username{};
    std::string password{};
    std::string api_token{};
};

std::map<std::string, RemoteCredentialSecret> loadCredentialSecrets(const fs::path& path)
{
    std::map<std::string, RemoteCredentialSecret> secrets{};
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        return secrets;
    }

    std::string line{};
    while (std::getline(input, line)) {
        if (line.empty() || line[0] == '#') {
            continue;
        }
        const auto values = splitTabLine(line);
        if (values.size() < 4U) {
            continue;
        }
        secrets.emplace(
            unescapeStoredValue(values[0]),
            RemoteCredentialSecret{
                unescapeStoredValue(values[1]),
                unescapeStoredValue(values[2]),
                unescapeStoredValue(values[3])});
    }
    return secrets;
}

std::string lineForCredential(std::string_view id, const RemoteCredentialSecret& secret)
{
    std::ostringstream out;
    const char separator = '\t';
    out << runtime_detail::jsonEscape(id) << separator
        << runtime_detail::jsonEscape(secret.username) << separator
        << runtime_detail::jsonEscape(secret.password) << separator
        << runtime_detail::jsonEscape(secret.api_token);
    return out.str();
}

} // namespace

XdgRemoteProfileStore::XdgRemoteProfileStore(std::filesystem::path profile_path, std::filesystem::path credential_path)
    : profile_path_(std::move(profile_path)),
      credential_path_(std::move(credential_path))
{
    if (profile_path_.empty()) {
        profile_path_ = runtime_paths::appDataDir() / "remote-profiles.tsv";
    }
    if (credential_path_.empty()) {
        credential_path_ = runtime_paths::appStateDir() / "remote-credentials.tsv";
    }
}

std::vector<app::RemoteServerProfile> XdgRemoteProfileStore::loadProfiles() const
{
    std::vector<app::RemoteServerProfile> profiles{};
    std::ifstream input(profile_path_, std::ios::binary);
    if (!input) {
        return profiles;
    }

    const auto secrets = loadCredentialSecrets(credential_path_);
    std::string line{};
    while (std::getline(input, line)) {
        if (line.empty() || line[0] == '#') {
            continue;
        }
        const auto values = splitTabLine(line);
        if (values.size() < 8U) {
            continue;
        }
        app::RemoteServerProfile profile{};
        profile.kind = app::remoteServerKindFromString(unescapeStoredValue(values[0]));
        profile.id = unescapeStoredValue(values[1]);
        profile.name = unescapeStoredValue(values[2]);
        profile.base_url = unescapeStoredValue(values[3]);
        profile.username = unescapeStoredValue(values[4]);
        profile.credential_ref.id = unescapeStoredValue(values[5]);
        profile.tls_policy.verify_peer = values[6] != "false";
        profile.tls_policy.allow_self_signed = values[7] == "true";
        if (values.size() >= 9U) {
            profile.local_root = unescapeStoredValue(values[8]);
        }
        if (values.size() >= 10U) {
            profile.default_eligible = values[9] != "false";
        }
        if (values.size() >= 11U) {
            profile.enabled = values[10] != "false";
        }
        if (profile.kind == app::RemoteServerKind::LocalRoot) {
            if (profile.local_root.empty()) {
                profile.local_root = profile.base_url;
            }
            if (profile.base_url.empty()) {
                profile.base_url = profile.local_root;
            }
            profile.username.clear();
            profile.password.clear();
            profile.api_token.clear();
            profile.credential_ref.id.clear();
        }
        if (profile.kind != app::RemoteServerKind::LocalRoot) {
            const auto secret_id = profile.credential_ref.id.empty() ? profile.id : profile.credential_ref.id;
            if (const auto found = secrets.find(secret_id); found != secrets.end()) {
                if (!found->second.username.empty()) {
                    profile.username = found->second.username;
                }
                profile.password = found->second.password;
                profile.api_token = found->second.api_token;
            }
        }
        profiles.push_back(std::move(profile));
    }
    return profiles;
}

bool XdgRemoteProfileStore::saveProfiles(const std::vector<app::RemoteServerProfile>& profiles) const
{
    std::error_code ec{};
    fs::create_directories(profile_path_.parent_path(), ec);
    if (ec) {
        return false;
    }
    std::ofstream output(profile_path_, std::ios::binary | std::ios::trunc);
    if (!output) {
        return false;
    }
    output << "# LoFiBox remote profiles v1\n";
    for (const auto& profile : profiles) {
        output << lineForProfile(profile) << '\n';
    }
    return static_cast<bool>(output);
}

bool XdgRemoteProfileStore::saveCredentials(const app::RemoteServerProfile& profile) const
{
    const auto secret_id = profile.credential_ref.id.empty() ? profile.id : profile.credential_ref.id;
    if (secret_id.empty()) {
        return false;
    }

    auto secrets = loadCredentialSecrets(credential_path_);
    auto& secret = secrets[secret_id];
    if (!profile.username.empty()) {
        secret.username = profile.username;
    }
    if (!profile.password.empty()) {
        secret.password = profile.password;
    }
    if (!profile.api_token.empty()) {
        secret.api_token = profile.api_token;
    }

    std::error_code ec{};
    fs::create_directories(credential_path_.parent_path(), ec);
    if (ec) {
        return false;
    }

    std::ofstream output(credential_path_, std::ios::binary | std::ios::trunc);
    if (!output) {
        return false;
    }
    output << "# LoFiBox remote credential references v1\n";
    for (const auto& [id, value] : secrets) {
        output << lineForCredential(id, value) << '\n';
    }
    return static_cast<bool>(output);
}

bool XdgRemoteProfileStore::deleteCredentials(const ::lofibox::security::CredentialRef& credential_ref) const
{
    if (credential_ref.id.empty()) {
        return false;
    }
    auto secrets = loadCredentialSecrets(credential_path_);
    const auto removed = secrets.erase(credential_ref.id);

    std::error_code ec{};
    fs::create_directories(credential_path_.parent_path(), ec);
    if (ec) {
        return false;
    }

    std::ofstream output(credential_path_, std::ios::binary | std::ios::trunc);
    if (!output) {
        return false;
    }
    output << "# LoFiBox remote credential references v1\n";
    for (const auto& [id, value] : secrets) {
        output << lineForCredential(id, value) << '\n';
    }
    return removed > 0U || static_cast<bool>(output);
}

const std::filesystem::path& XdgRemoteProfileStore::profilePath() const noexcept
{
    return profile_path_;
}

} // namespace lofibox::platform::host
