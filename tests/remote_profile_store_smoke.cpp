// SPDX-License-Identifier: GPL-3.0-or-later

#include "platform/host/xdg_remote_profile_store.h"

#include <cassert>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

int main()
{
    const auto root = std::filesystem::temp_directory_path() / "lofibox_remote_profile_store_smoke";
    std::error_code ec{};
    std::filesystem::remove_all(root, ec);
    std::filesystem::create_directories(root, ec);

    const auto path = root / "remote-profiles.tsv";
    const auto credential_path = root / "remote-credentials.tsv";
    lofibox::platform::host::XdgRemoteProfileStore store{path, credential_path};

    lofibox::app::RemoteServerProfile profile{};
    profile.kind = lofibox::app::RemoteServerKind::Navidrome;
    profile.id = "home-nav";
    profile.name = "Home Navidrome";
    profile.base_url = "https://music.example";
    profile.username = "vic";
    profile.password = "must-not-persist";
    profile.api_token = "must-not-persist-token";
    profile.credential_ref.id = "secret/home-nav";
    profile.tls_policy.verify_peer = true;

    assert(store.saveProfiles({profile}));
    const auto loaded = store.loadProfiles();
    assert(loaded.size() == 1U);
    assert(loaded.front().kind == lofibox::app::RemoteServerKind::Navidrome);
    assert(loaded.front().credential_ref.id == "secret/home-nav");
    assert(loaded.front().password.empty());
    assert(loaded.front().api_token.empty());

    std::ifstream input(path, std::ios::binary);
    const std::string content((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    assert(content.find("must-not-persist") == std::string::npos);
    assert(content.find("must-not-persist-token") == std::string::npos);

    assert(store.saveCredentials(profile));
    const auto loaded_with_secret = store.loadProfiles();
    assert(loaded_with_secret.size() == 1U);
    assert(loaded_with_secret.front().username == "vic");
    assert(loaded_with_secret.front().password == "must-not-persist");
    assert(loaded_with_secret.front().api_token == "must-not-persist-token");

    std::ifstream credential_input(credential_path, std::ios::binary);
    const std::string credential_content((std::istreambuf_iterator<char>(credential_input)), std::istreambuf_iterator<char>());
    assert(credential_content.find("secret/home-nav") != std::string::npos);
    assert(credential_content.find("must-not-persist-token") != std::string::npos);

    assert(store.deleteCredentials(profile.credential_ref));
    const auto loaded_after_delete = store.loadProfiles();
    assert(loaded_after_delete.size() == 1U);
    assert(loaded_after_delete.front().password.empty());
    assert(loaded_after_delete.front().api_token.empty());

    lofibox::app::RemoteServerProfile local_root{};
    local_root.kind = lofibox::app::RemoteServerKind::LocalRoot;
    local_root.id = "local-root-home";
    local_root.name = "Home Music";
    local_root.local_root = (root / "Music").string();
    local_root.base_url = local_root.local_root;
    local_root.default_eligible = false;
    local_root.enabled = false;
    local_root.credential_ref.id = "must-not-persist";

    assert(store.saveProfiles({local_root}));
    const auto loaded_local_roots = store.loadProfiles();
    assert(loaded_local_roots.size() == 1U);
    assert(loaded_local_roots.front().kind == lofibox::app::RemoteServerKind::LocalRoot);
    assert(loaded_local_roots.front().local_root == (root / "Music").string());
    assert(loaded_local_roots.front().base_url == loaded_local_roots.front().local_root);
    assert(!loaded_local_roots.front().default_eligible);
    assert(!loaded_local_roots.front().enabled);
    assert(loaded_local_roots.front().credential_ref.id.empty());

    std::filesystem::remove_all(root, ec);
    return 0;
}
