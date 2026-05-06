// SPDX-License-Identifier: GPL-3.0-or-later

#include <exception>
#include <chrono>
#include <iostream>
#include <utility>

#include "app/lofibox_app_runner.h"
#include "cli/direct_cli.h"
#include "cli/runtime_cli.h"
#include "platform/host/legacy_asset_loader.h"
#include "platform/host/runtime_paths.h"
#include "platform/host/runtime_services_factory.h"
#include "platform/host/single_instance_lock.h"
#include "platform/x11/x11_presenter.h"
#include "targets/cli_options.h"
#if defined(LOFIBOX_HAVE_TUI)
#include "tui/tui_app.h"
#endif
#include "webui/webui_config.h"
#if defined(LOFIBOX_HAVE_WEBUI)
#include "application/library_enrich_provider_adapter.h"
#include "application/library_query_provider_adapter.h"
#include "webui/webui_runtime_adapter.h"
#include "webui/webui_server.h"
#endif

int main(int argc, char** argv)
{
    try {
        if (lofibox::targets::handleCommonCliOptions(argc, argv, std::cout)) {
            return 0;
        }

#if defined(LOFIBOX_HAVE_TUI)
        if (const auto tui_exit = lofibox::tui::runTuiSubcommandFromArgv(argc, argv, std::cout, std::cerr)) {
            return *tui_exit;
        }
#endif

        if (const auto runtime_cli_exit = lofibox::cli::runRuntimeCliCommand(argc, argv, std::cout, std::cerr)) {
            return *runtime_cli_exit;
        }

        auto services = lofibox::platform::host::createHostRuntimeServices();
        if (const auto direct_cli_exit = lofibox::cli::runDirectCliCommand(argc, argv, services, std::cout, std::cerr)) {
            return *direct_cli_exit;
        }

        lofibox::webui::WebUiConfig webui_config{};
        lofibox::webui::parseWebUiFromArgs(argc, argv, webui_config);

        lofibox::app::AppStarter on_start;
#if defined(LOFIBOX_HAVE_WEBUI)
        if (webui_config.enabled) {
            auto webui_ctx = std::make_shared<
                std::pair<std::unique_ptr<lofibox::webui::WebUiRuntimeAdapter>,
                          std::unique_ptr<lofibox::webui::WebUiServer>>>();
            auto lib_provider = std::make_shared<lofibox::application::LibraryQueryProviderAdapter>(
                services.metadata.artwork_provider,
                lofibox::platform::host::runtime_paths::appCacheDir());
            auto enrich = std::make_shared<lofibox::application::LibraryEnrichProviderAdapter>(services.cache.cache_manager);
            on_start = [cfg = std::move(webui_config), webui_ctx, lib_provider, enrich]
                       (lofibox::runtime::RuntimeCommandClient& client,
                        const ::lofibox::application::AppServiceRegistry& registry) mutable {
                lib_provider->bind(registry.libraryQueries());
                webui_ctx->first = std::make_unique<lofibox::webui::WebUiRuntimeAdapter>(client);
                webui_ctx->second = std::make_unique<lofibox::webui::WebUiServer>(std::move(cfg), *webui_ctx->first);
                webui_ctx->second->setLibraryQueryProvider(lib_provider.get());
                webui_ctx->second->setLibraryEnrichProvider(enrich.get());
                webui_ctx->second->start();
            };
        }
#endif

        auto instance_lock = lofibox::platform::host::SingleInstanceLock::acquire();
        if (!instance_lock.acquired()) {
            std::cerr << "X11 startup skipped: " << instance_lock.message() << '\n';
            return 2;
        }
        lofibox::platform::x11::X11Presenter presenter{};
        auto assets = lofibox::platform::host::loadLegacyAssets();
        lofibox::app::runLoFiBoxApp(
            presenter,
            std::move(assets),
            std::move(services),
            std::chrono::milliseconds::zero(),
            lofibox::targets::positionalOpenUris(argc, argv),
            std::move(on_start)
        );
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "X11 startup failed: " << ex.what() << '\n';
        return 1;
    }
}
