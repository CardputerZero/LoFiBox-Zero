// SPDX-License-Identifier: GPL-3.0-or-later

#include <cassert>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#if !defined(_WIN32)
#include <pwd.h>
#include <sys/types.h>
#include <unistd.h>
#endif

#include "app/lofibox_app.h"
#include "app/remote_profile_store.h"
#include "app/runtime_services.h"
#include "application/app_service_host.h"
#include "application/source_profile_command_service.h"

namespace {

class MemoryRemoteProfileStore final : public lofibox::app::RemoteProfileStore {
public:
    std::vector<lofibox::app::RemoteServerProfile> loadProfiles() const override
    {
        return profiles;
    }

    bool saveProfiles(const std::vector<lofibox::app::RemoteServerProfile>& value) const override
    {
        profiles = value;
        return true;
    }

    bool deleteCredentials(const lofibox::security::CredentialRef&) const override
    {
        return true;
    }

    mutable std::vector<lofibox::app::RemoteServerProfile> profiles{};
};

class FakeMetadataProvider final : public lofibox::app::MetadataProvider {
public:
    [[nodiscard]] bool available() const override { return true; }
    [[nodiscard]] std::string displayName() const override { return "LOCAL-ROOT-METADATA"; }
    [[nodiscard]] lofibox::app::TrackMetadata read(const std::filesystem::path&, lofibox::app::MetadataReadMode = lofibox::app::MetadataReadMode::AllowOnline) const override
    {
        lofibox::app::TrackMetadata metadata{};
        metadata.title = "Local Root Song";
        metadata.artist = "Local Root Artist";
        metadata.album = "Local Root Album";
        return metadata;
    }
};

void setHome(const std::filesystem::path& home)
{
#if defined(_WIN32)
    _putenv_s("HOME", home.string().c_str());
#else
    setenv("HOME", home.string().c_str(), 1);
#endif
}

#if !defined(_WIN32)
class EnvVarGuard {
public:
    explicit EnvVarGuard(const char* name)
        : name_(name)
    {
        if (const char* value = std::getenv(name_); value != nullptr) {
            value_ = std::string{value};
        }
    }

    ~EnvVarGuard()
    {
        if (value_) {
            setenv(name_, value_->c_str(), 1);
            return;
        }
        unsetenv(name_);
    }

    EnvVarGuard(const EnvVarGuard&) = delete;
    EnvVarGuard& operator=(const EnvVarGuard&) = delete;

private:
    const char* name_{};
    std::optional<std::string> value_{};
};

std::filesystem::path accountHomeDir()
{
    const auto* entry = getpwuid(geteuid());
    if (entry == nullptr || entry->pw_dir == nullptr || *entry->pw_dir == '\0') {
        return {};
    }
    return entry->pw_dir;
}
#endif

std::filesystem::path normalized(const std::filesystem::path& path)
{
    std::error_code ec{};
    const auto canonical = std::filesystem::weakly_canonical(path, ec);
    return ec ? path.lexically_normal() : canonical;
}

void touchFile(const std::filesystem::path& path)
{
    std::ofstream output{path, std::ios::binary};
    output << "test";
}

} // namespace

int main()
{
    const auto root = std::filesystem::temp_directory_path() / "lofibox_local_root_source_profile_smoke";
    const auto home = root / "home";
    const auto default_music = home / "Music";
    const auto alt_music = root / "AltMusic";
    std::error_code ec{};
    std::filesystem::remove_all(root, ec);
    std::filesystem::create_directories(default_music, ec);
    std::filesystem::create_directories(alt_music, ec);
    touchFile(default_music / "Default.mp3");
    touchFile(alt_music / "Alt.mp3");
    setHome(home);

    auto store = std::make_shared<MemoryRemoteProfileStore>();
    auto services = lofibox::app::withNullRuntimeServices();
    services.remote.remote_profile_store = store;
    services.metadata.metadata_provider = std::make_shared<FakeMetadataProvider>();

    lofibox::application::SourceProfileCommandService source_profiles{services};
    const auto default_roots = source_profiles.enabledLocalRoots();
    assert(default_roots.size() == 1U);
    assert(normalized(default_roots.front()) == normalized(default_music));

    const auto visible_roots = source_profiles.listLocalRoots();
    assert(visible_roots.size() == 1U);
    assert(visible_roots.front().id == "local-root-default");
    assert(visible_roots.front().enabled);
    assert(normalized(visible_roots.front().local_root) == normalized(default_music));

#if !defined(_WIN32)
    {
        EnvVarGuard home_guard{"HOME"};
        unsetenv("HOME");
        const auto account_home = accountHomeDir();
        assert(!account_home.empty());
        assert(normalized(source_profiles.defaultLocalRoot()) == normalized(account_home / "Music"));
    }
#endif

    lofibox::application::AppServiceHost app_host{services};
    assert(app_host.registry().libraryMutations().refreshConfiguredLibrary());
    assert(app_host.registry().libraryQueries().model().tracks.size() == 1U);
    assert(app_host.registry().libraryQueries().model().tracks.front().path.filename() == "Default.mp3");

    const auto added = source_profiles.addLocalRoot(alt_music, "Alt");
    assert(added.command.accepted);
    assert(added.profile.kind == lofibox::app::RemoteServerKind::LocalRoot);
    assert(added.profile.enabled);
    assert(added.profile.credential_ref.id.empty());

    const auto configured_roots = source_profiles.enabledLocalRoots();
    assert(configured_roots.size() == 1U);
    assert(normalized(configured_roots.front()) == normalized(alt_music));

    {
        lofibox::app::LoFiBoxApp app{{}, {}, services};
        for (int tick = 0; tick < 500; ++tick) {
            app.update();
            if (app.snapshot().library_ready) {
                break;
            }
        }
        const auto snapshot = app.snapshot();
        assert(snapshot.library_ready);
        assert(snapshot.track_count == 1U);
    }

    const auto disabled = source_profiles.disableLocalRoot(added.profile.id);
    assert(disabled.command.accepted);
    const auto fallback_roots = source_profiles.enabledLocalRoots();
    assert(fallback_roots.size() == 1U);
    assert(normalized(fallback_roots.front()) == normalized(default_music));

    std::filesystem::remove_all(root, ec);
    return 0;
}
