// SPDX-License-Identifier: GPL-3.0-or-later

#include <iostream>
#include <memory>
#include <utility>
#include <vector>

#include "app/app_runtime_context.h"
#include "app/input_event.h"
#include "app/remote_profile_store.h"
#include "app/runtime_services.h"
#include "application/app_service_host.h"
#include "runtime/runtime_host.h"

namespace {

class ReloadingRemoteProfileStore final : public lofibox::app::RemoteProfileStore {
public:
    std::vector<lofibox::app::RemoteServerProfile> loadProfiles() const override
    {
        ++load_count;
        if (load_count == 1) {
            return {};
        }

        lofibox::app::RemoteServerProfile profile{};
        profile.kind = lofibox::app::RemoteServerKind::DirectUrl;
        profile.id = "direct-lab";
        profile.name = "Direct URL Lab";
        profile.base_url = "http://192.168.50.48:18080/media/direct/track.mp3";
        profile.tls_policy.verify_peer = true;
        return {profile};
    }

    bool saveProfiles(const std::vector<lofibox::app::RemoteServerProfile>&) const override
    {
        return true;
    }

    mutable int load_count{0};
};

struct TestRuntimeApp {
    explicit TestRuntimeApp(lofibox::app::RuntimeServices initial_services = {})
        : services(lofibox::app::withNullRuntimeServices(std::move(initial_services))),
          app_host(services),
          runtime_host(app_host.registry()),
          app({}, {}, {}, app_host, runtime_host.client())
    {
    }

    lofibox::app::RuntimeServices services;
    lofibox::application::AppServiceHost app_host;
    lofibox::runtime::RuntimeHost runtime_host;
    lofibox::app::AppRuntimeContext app;
};

} // namespace

int main()
{
    TestRuntimeApp test_app{};
    auto& app = test_app.app;

    app.update();
    for (int tick = 0; tick < 500; ++tick) {
        app.update();
        if (app.snapshot().library_ready) {
            break;
        }
    }

    auto snapshot = app.snapshot();
    if (snapshot.current_page != lofibox::app::AppPage::MainMenu) {
        std::cerr << "Expected Boot to transition to Main Menu.\n";
        return 1;
    }

    for (int i = 0; i < 4; ++i) {
        app.handleInput(lofibox::app::InputEvent{lofibox::app::InputKey::Right, "RIGHT", '\0'});
    }
    app.handleInput(lofibox::app::InputEvent{lofibox::app::InputKey::Enter, "OK", '\0'});

    snapshot = app.snapshot();
    if (snapshot.current_page != lofibox::app::AppPage::Settings) {
        std::cerr << "Expected Main Menu to enter Settings.\n";
        return 1;
    }

    if (snapshot.visible_count != 7) {
        std::cerr << "Expected Settings rows to include one Remote Setup entry instead of leaking remote fields.\n";
        return 1;
    }

    app.handleInput(lofibox::app::InputEvent{lofibox::app::InputKey::Enter, "OK", '\0'});
    snapshot = app.snapshot();
    if (snapshot.current_page != lofibox::app::AppPage::Settings) {
        std::cerr << "Expected Network row to remain read-only.\n";
        return 1;
    }

    app.handleInput(lofibox::app::InputEvent{lofibox::app::InputKey::Down, "DOWN", '\0'});
    app.handleInput(lofibox::app::InputEvent{lofibox::app::InputKey::Enter, "OK", '\0'});
    snapshot = app.snapshot();
    if (snapshot.current_page != lofibox::app::AppPage::Settings) {
        std::cerr << "Expected Metadata Service row to remain read-only.\n";
        return 1;
    }

    app.handleInput(lofibox::app::InputEvent{lofibox::app::InputKey::Down, "DOWN", '\0'});
    app.handleInput(lofibox::app::InputEvent{lofibox::app::InputKey::Enter, "OK", '\0'});
    snapshot = app.snapshot();
    if (snapshot.current_page != lofibox::app::AppPage::Settings) {
        std::cerr << "Expected Sleep Timer to stay on Settings page.\n";
        return 1;
    }

    app.handleInput(lofibox::app::InputEvent{lofibox::app::InputKey::Down, "DOWN", '\0'});
    app.handleInput(lofibox::app::InputEvent{lofibox::app::InputKey::Down, "DOWN", '\0'});
    app.handleInput(lofibox::app::InputEvent{lofibox::app::InputKey::Down, "DOWN", '\0'});
    app.handleInput(lofibox::app::InputEvent{lofibox::app::InputKey::Enter, "OK", '\0'});
    snapshot = app.snapshot();
    if (snapshot.current_page != lofibox::app::AppPage::RemoteSetup) {
        std::cerr << "Expected Remote Setup to first list supported remote media types.\n";
        return 1;
    }
    auto setup_rows = app.pageModel().rows;
    if (setup_rows.size() < 15U
        || setup_rows[0].first != "Jellyfin"
        || setup_rows[3].first != "Emby"
        || setup_rows[10].first != "NFS"
        || setup_rows[14].first != "DLNA / UPnP") {
        std::cerr << "Expected Settings Remote Setup to list all supported remote media kinds before details.\n";
        return 1;
    }

    app.handleInput(lofibox::app::InputEvent{lofibox::app::InputKey::Enter, "OK", '\0'});
    snapshot = app.snapshot();
    if (snapshot.current_page != lofibox::app::AppPage::RemoteProfileSettings) {
        std::cerr << "Expected selecting a remote media kind to open that kind's concrete settings.\n";
        return 1;
    }
    auto remote_rows = app.pageModel().rows;
    if (remote_rows.size() < 8U
        || remote_rows[3].first != "ADDRESS"
        || remote_rows[4].first != "USER"
        || remote_rows[5].first != "PASSWORD"
        || remote_rows[7].first != "TLS VERIFY") {
        std::cerr << "Expected Settings remote media surface to expose address, user, credential, and TLS fields.\n";
        return 1;
    }

    {
        lofibox::app::RuntimeServices services{};
        services.remote.remote_profile_store = std::make_shared<ReloadingRemoteProfileStore>();
        TestRuntimeApp configured_test_app{std::move(services)};
        auto& configured_app = configured_test_app.app;

        configured_app.update();
        for (int tick = 0; tick < 500; ++tick) {
            configured_app.update();
            if (configured_app.snapshot().library_ready) {
                break;
            }
        }
        configured_app.openSettingsPage();
        configured_app.handleSettingsRemoteConfirm(0);

        const auto configured_setup_rows = configured_app.pageModel().rows;
        if (configured_setup_rows.size() < 5U || configured_setup_rows[4].first != "Direct URL" || configured_setup_rows[4].second != "READY") {
            std::cerr << "Expected Remote Setup to reload persisted profiles and show configured direct URL readiness.\n";
            return 1;
        }

        configured_app.handleRemoteSetupConfirm(4);
        const auto direct_rows = configured_app.pageModel().rows;
        if (direct_rows.size() < 7U
            || direct_rows[3].second != "http://192.168.50.48:18080/media/direct/track.mp3"
            || direct_rows[4].second != "N/A"
            || direct_rows[5].second != "N/A"
            || direct_rows[6].second != "N/A") {
            std::cerr << "Expected credential-free source settings to show the persisted URL and non-applicable credential rows.\n";
            return 1;
        }
    }

    app.handleInput(lofibox::app::InputEvent{lofibox::app::InputKey::Backspace, "BACK", '\0'});
    app.handleInput(lofibox::app::InputEvent{lofibox::app::InputKey::Backspace, "BACK", '\0'});
    for (int i = 0; i < 6; ++i) {
        app.handleInput(lofibox::app::InputEvent{lofibox::app::InputKey::Down, "DOWN", '\0'});
    }
    app.handleInput(lofibox::app::InputEvent{lofibox::app::InputKey::Enter, "OK", '\0'});
    snapshot = app.snapshot();
    if (snapshot.current_page != lofibox::app::AppPage::About) {
        std::cerr << "Expected About row to open About page.\n";
        return 1;
    }

    return 0;
}
