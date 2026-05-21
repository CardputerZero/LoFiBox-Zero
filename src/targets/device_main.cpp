// SPDX-License-Identifier: GPL-3.0-or-later

#include <cstdlib>
#include <chrono>
#include <exception>
#include <iostream>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

#include "app/lofibox_app_runner.h"
#include "cli/direct_cli.h"
#include "cli/runtime_cli.h"
#include "platform/host/legacy_asset_loader.h"
#include "platform/host/runtime_paths.h"
#include "platform/host/runtime_services_factory.h"
#include "platform/host/single_instance_lock.h"
#include "platform/device/linux_framebuffer_platform.h"
#include "targets/cli_options.h"
#if defined(LOFIBOX_HAVE_TUI)
#include "tui/tui_app.h"
#endif
#if defined(LOFIBOX_HAVE_WEBUI)
#include "platform/host/library_enrich_provider_adapter.h"
#include "platform/host/library_query_provider_adapter.h"
#include "webui/webui_config.h"
#include "webui/webui_runtime_adapter.h"
#include "webui/webui_server.h"
#endif

namespace {

struct DeviceOptions {
    std::string framebuffer_path{"/dev/fb0"};
    std::string input_device_path{"/dev/input/by-path/platform-3f804000.i2c-event"};
};

std::string envOrDefault(const char* name, std::string fallback)
{
    if (const char* env = std::getenv(name)) {
        if (*env != '\0') {
            return env;
        }
    }

    return fallback;
}

DeviceOptions parseOptions(int argc, char** argv)
{
    DeviceOptions options{};
    options.framebuffer_path = envOrDefault("LOFIBOX_FBDEV", options.framebuffer_path);
    options.input_device_path = envOrDefault("LOFIBOX_INPUT_DEV", options.input_device_path);

    for (int index = 1; index < argc; ++index) {
        const std::string_view current{argv[index]};
        if (current == "--fbdev" && (index + 1) < argc) {
            options.framebuffer_path = argv[index + 1];
            ++index;
            continue;
        }

        if (current == "--input-dev" && (index + 1) < argc) {
            options.input_device_path = argv[index + 1];
            ++index;
        }
    }

    return options;
}

} // namespace

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

        lofibox::app::AppStarter on_start;
#if defined(LOFIBOX_HAVE_WEBUI)
        lofibox::webui::WebUiConfig webui_config{};
        lofibox::webui::parseWebUiFromArgs(argc, argv, webui_config);
        if (webui_config.enabled) {
            services.ui.webui_url = lofibox::webui::displayUrlForWebUi(webui_config);
            auto webui_ctx = std::make_shared<
                std::pair<std::unique_ptr<lofibox::webui::WebUiRuntimeAdapter>,
                          std::unique_ptr<lofibox::webui::WebUiServer>>>();
            auto lib_provider = std::make_shared<lofibox::platform::host::LibraryQueryProviderAdapter>(
                services.metadata.artwork_provider,
                lofibox::platform::host::runtime_paths::appCacheDir());
            auto enrich = std::make_shared<lofibox::platform::host::LibraryEnrichProviderAdapter>(services.cache.cache_manager);
            auto active_theme = services.ui.theme;
            on_start = [cfg = std::move(webui_config), webui_ctx, lib_provider, enrich, active_theme]
                       (lofibox::runtime::RuntimeCommandClient& client,
                        const ::lofibox::application::AppServiceRegistry& registry) mutable {
                lib_provider->bind(registry.libraryQueries());
                webui_ctx->first = std::make_unique<lofibox::webui::WebUiRuntimeAdapter>(client);
                webui_ctx->second = std::make_unique<lofibox::webui::WebUiServer>(std::move(cfg), *webui_ctx->first);
                webui_ctx->second->setLibraryQueryProvider(lib_provider.get());
                webui_ctx->second->setLibraryEnrichProvider(enrich.get());
                webui_ctx->second->setTheme(active_theme.get());
                webui_ctx->second->start();
            };
        }
#endif

        const DeviceOptions options = parseOptions(argc, argv);
        auto instance_lock = lofibox::platform::host::SingleInstanceLock::acquire();
        if (!instance_lock.acquired()) {
            std::cerr << "Device startup skipped: " << instance_lock.message() << '\n';
            return 2;
        }
        lofibox::platform::device::LinuxFramebufferPlatform device{
            std::move(options.framebuffer_path),
            std::move(options.input_device_path)};
        auto assets = lofibox::platform::host::loadLegacyAssets();
        lofibox::app::runLoFiBoxApp(
            device,
            std::move(assets),
            std::move(services),
            std::chrono::milliseconds::zero(),
            lofibox::targets::positionalOpenUris(argc, argv),
            std::move(on_start)
        );
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "Device startup failed: " << ex.what() << '\n';
        return 1;
    }
}
