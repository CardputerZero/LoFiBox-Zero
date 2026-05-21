// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "app/remote_media_services.h"
#include "app/runtime_services.h"
#include "application/app_command_result.h"

namespace lofibox::application {

enum class SourceProfileCredentialMode {
    None,
    Optional,
    Required,
};

enum class SourceProfileTextField {
    Label,
    Address,
    Username,
    Password,
    ApiToken,
};

struct SourceProfileProbeResult {
    CommandResult command{};
    app::RemoteSourceSession session{};
};

struct LocalRootCommandResult {
    CommandResult command{};
    app::RemoteServerProfile profile{};
};

class SourceProfileCommandService {
public:
    explicit SourceProfileCommandService(const app::RuntimeServices& services) noexcept;

    [[nodiscard]] std::vector<app::RemoteServerProfile> loadProfiles() const;
    [[nodiscard]] bool persistProfiles(std::vector<app::RemoteServerProfile>& profiles) const;

    [[nodiscard]] app::RemoteServerProfile createProfile(app::RemoteServerKind kind, std::size_t existing_profile_count) const;
    [[nodiscard]] std::size_t ensureProfileForKind(std::vector<app::RemoteServerProfile>& profiles, app::RemoteServerKind kind) const;
    [[nodiscard]] std::optional<std::size_t> findProfileByKind(const std::vector<app::RemoteServerProfile>& profiles, app::RemoteServerKind kind) const;

    void ensureCredentialRef(app::RemoteServerProfile& profile, std::size_t profile_count) const;
    [[nodiscard]] bool updateTextField(app::RemoteServerProfile& profile, SourceProfileTextField field, std::string_view value, std::size_t profile_count) const;
    [[nodiscard]] bool toggleTlsVerify(app::RemoteServerProfile& profile, std::vector<app::RemoteServerProfile>& profiles) const;
    [[nodiscard]] bool toggleSelfSigned(app::RemoteServerProfile& profile, std::vector<app::RemoteServerProfile>& profiles) const;
    [[nodiscard]] SourceProfileProbeResult probe(app::RemoteServerProfile& profile, std::size_t profile_count) const;

    [[nodiscard]] std::vector<app::RemoteServerProfile> listLocalRoots() const;
    [[nodiscard]] std::vector<std::filesystem::path> enabledLocalRoots() const;
    [[nodiscard]] LocalRootCommandResult addLocalRoot(const std::filesystem::path& path, std::optional<std::string> name = std::nullopt) const;
    [[nodiscard]] LocalRootCommandResult removeLocalRoot(std::string_view id_or_path) const;
    [[nodiscard]] LocalRootCommandResult enableLocalRoot(std::string_view id_or_path) const;
    [[nodiscard]] LocalRootCommandResult disableLocalRoot(std::string_view id_or_path) const;

    [[nodiscard]] std::string kindDisplayName(app::RemoteServerKind kind) const;
    [[nodiscard]] std::string defaultProfileId(app::RemoteServerKind kind, std::size_t index) const;
    [[nodiscard]] std::string defaultProfileName(app::RemoteServerKind kind) const;
    [[nodiscard]] std::string profileLabel(const app::RemoteServerProfile& profile) const;
    [[nodiscard]] SourceProfileCredentialMode credentialMode(app::RemoteServerKind kind) const noexcept;
    [[nodiscard]] bool supportsCredentials(app::RemoteServerKind kind) const noexcept;
    [[nodiscard]] std::string readiness(const app::RemoteServerProfile& profile) const;
    [[nodiscard]] std::string credentialValueLabel(app::RemoteServerKind kind, std::string_view value) const;
    [[nodiscard]] std::string usernameLabel(const app::RemoteServerProfile& profile) const;
    [[nodiscard]] std::string permissionLabel(app::RemoteServerKind kind) const;
    [[nodiscard]] bool keepsLocalFacts(app::RemoteServerKind kind) const;
    [[nodiscard]] std::filesystem::path defaultLocalRoot() const;
    [[nodiscard]] std::filesystem::path localRootPath(const app::RemoteServerProfile& profile) const;

private:
    const app::RuntimeServices& services_;
};

} // namespace lofibox::application
