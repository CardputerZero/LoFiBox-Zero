# SPDX-License-Identifier: GPL-3.0-or-later

cmake_minimum_required(VERSION 3.24)

project(LoFiBoxZero VERSION 0.2.4 LANGUAGES C CXX)

if(DEFINED CMAKE_EXPORT_NO_PACKAGE_REGISTRY)
    set(_lofibox_cmake_export_no_package_registry "${CMAKE_EXPORT_NO_PACKAGE_REGISTRY}")
endif()
if(DEFINED CMAKE_FIND_PACKAGE_NO_PACKAGE_REGISTRY)
    set(_lofibox_cmake_find_package_no_package_registry "${CMAKE_FIND_PACKAGE_NO_PACKAGE_REGISTRY}")
endif()
if(DEFINED FETCHCONTENT_FULLY_DISCONNECTED)
    set(_lofibox_fetchcontent_fully_disconnected "${FETCHCONTENT_FULLY_DISCONNECTED}")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
    add_compile_options(/utf-8)
endif()

include(GNUInstallDirs)
include(CTest)
find_package(Threads REQUIRED)

set(LOFIBOX_PRIVATE_LIBDIR "lib/lofibox" CACHE PATH "Private LoFiBox runtime directory")
set(CMAKE_INSTALL_DOCDIR "${CMAKE_INSTALL_DATAROOTDIR}/doc/lofibox")

set(LOFIBOX_BUILD_DEVICE_DEFAULT OFF)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(LOFIBOX_BUILD_DEVICE_DEFAULT ON)
endif()

option(LOFIBOX_BUILD_DEVICE "Build the Linux framebuffer device target." ${LOFIBOX_BUILD_DEVICE_DEFAULT})
option(LOFIBOX_BUILD_X11 "Build the Linux X11 VNC/PocketFrame target." ${LOFIBOX_BUILD_DEVICE_DEFAULT})
option(LOFIBOX_BUILD_TUI "Build the ANSI terminal UI target." ON)
option(LOFIBOX_BUILD_WEBUI "Build the WebUI remote-control HTTP/WebSocket server." OFF)

add_library(lofibox_zero_core STATIC
    src/audio/audio_pipeline_controller.cpp
    src/cache/cache_manager.cpp
    src/audio/decoder/audio_decoder_contract.cpp
        src/audio/output/audio_output_contract.cpp
    src/audio/dsp/audio_effect_registry.cpp
    src/audio/dsp/dsp_chain.cpp
    src/audio/dsp/realtime_dsp_engine.cpp
    src/app/input_actions.cpp
    src/app/app_command_executor.cpp
    src/app/app_lifecycle.cpp
    src/app/app_input_router.cpp
    src/app/app_page_model.cpp
    src/app/app_projection_builder.cpp
    src/app/app_renderer.cpp
    src/app/app_runtime_context.cpp
    src/application/app_service_registry.cpp
    src/application/cache_command_service.cpp
    src/application/credential_command_service.cpp
    src/application/library_mutation_service.cpp
    src/application/library_open_action_service.cpp
    src/application/library_query_service.cpp
    src/application/playback_command_service.cpp
    src/application/playback_status_query_service.cpp
    src/application/queue_command_service.cpp
    src/application/remote_browse_query_service.cpp
    src/application/runtime_diagnostic_service.cpp
    src/application/source_profile_command_service.cpp
    src/runtime/eq_runtime.cpp
    src/runtime/creator_analysis_runtime.cpp
    src/runtime/in_process_runtime_client.cpp
    src/runtime/playback_runtime.cpp
    src/runtime/queue_runtime.cpp
    src/runtime/remote_session_runtime.cpp
    src/runtime/runtime_command_bus.cpp
    src/runtime/runtime_command_dispatcher.cpp
    src/runtime/runtime_command_server.cpp
    src/runtime/runtime_event.cpp
    src/runtime/runtime_envelope_serializer.cpp
    src/runtime/runtime_host.cpp
    src/runtime/runtime_query_dispatcher.cpp
    src/runtime/runtime_result.cpp
    src/runtime/runtime_session_facade.cpp
    src/runtime/runtime_snapshot_assembler.cpp
    src/runtime/settings_runtime.cpp
    src/runtime/unix_socket_runtime_transport.cpp
    src/app/library_controller.cpp
    src/app/library_navigation_service.cpp
    src/app/library_repository.cpp
    src/app/library_scanner.cpp
    src/app/lofibox_app.cpp
    src/app/lofibox_app_runner.cpp
    src/app/media_item.cpp
    src/app/media_search_service.cpp
    src/app/remote_media_contract.cpp
    src/playback/mixed_playback_queue.cpp
    src/playback/playback_enrichment_coordinator.cpp
    src/playback/playback_backend_controller.cpp
    src/playback/playback_completion_policy.cpp
    src/playback/playback_runtime_coordinator.cpp
    src/playback/playback_session_clock.cpp
    src/playback/playback_visualization_source.cpp
    src/app/remote_profile_store.cpp
    src/app/settings_projection_builder.cpp
    src/app/source_manager_projection.cpp
    src/app/text_input_utils.cpp
    src/ui/pages/about_page.cpp
    src/ui/pages/equalizer_page.cpp
    src/ui/pages/list_page.cpp
    src/ui/pages/lyrics_page.cpp
    src/ui/pages/main_menu_page.cpp
    src/ui/pages/now_playing_page.cpp
    src/ui/effects/lyrics_spectrum_effect.cpp
    src/ui/widgets/lyrics_layout.cpp
    src/playback/playback_controller.cpp
    src/app/runtime_services.cpp
    src/metadata/enrichment_authority_policy.cpp
    src/metadata/match_confidence_guard.cpp
    src/metadata/metadata_governance.cpp
    src/metadata/metadata_merge_policy.cpp
    src/remote/common/remote_catalog_model.cpp
    src/remote/common/remote_provider_contract.cpp
    src/remote/common/remote_source_registry.cpp
    src/remote/common/stream_source_model.cpp
        src/playlist/playlist_parser.cpp
        src/plugins/plugin_manifest.cpp
        src/plugins/plugin_discovery.cpp
        src/plugins/skin_plugin_adapter.cpp
        src/plugins/plugin_runtime.cpp
        src/plugins/remote_plugin_adapter.cpp
    src/desktop/desktop_integration_boundary.cpp
    src/security/credential_policy.cpp
    src/ui/ui_primitives.cpp
    src/core/bitmap_font.cpp
    src/core/canvas.cpp
    src/library/library_governance.cpp
    src/library/library_indexer.cpp
    src/library/library_store.cpp
    src/playback/playback_stability_policy.cpp
    src/playback/streaming_playback_policy.cpp
)

target_include_directories(lofibox_zero_core PUBLIC "${PROJECT_SOURCE_DIR}/src")
target_compile_features(lofibox_zero_core PUBLIC cxx_std_20)
target_link_libraries(lofibox_zero_core PUBLIC Threads::Threads)
target_compile_definitions(lofibox_zero_core PUBLIC
    LOFIBOX_VERSION="${PROJECT_VERSION}"
    LOFIBOX_ASSET_DIR="${PROJECT_SOURCE_DIR}/assets"
    LOFIBOX_SYSTEM_ASSET_DIR="${CMAKE_INSTALL_FULL_DATADIR}/lofibox/assets"
)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    find_package(Freetype REQUIRED)
    target_link_libraries(lofibox_zero_core PUBLIC Freetype::Freetype)
    target_compile_definitions(lofibox_zero_core PRIVATE LOFIBOX_HAVE_FREETYPE=1)
endif()

if(MSVC)
    target_compile_options(lofibox_zero_core PRIVATE /W4 /permissive- /FS)
else()
    target_compile_options(lofibox_zero_core PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_library(lofibox_zero_host_lock STATIC
    src/platform/host/runtime_paths.cpp
    src/platform/host/single_instance_lock.cpp
)
target_include_directories(lofibox_zero_host_lock PUBLIC "${PROJECT_SOURCE_DIR}/src")
target_compile_features(lofibox_zero_host_lock PUBLIC cxx_std_20)
target_link_libraries(lofibox_zero_host_lock PUBLIC Threads::Threads)
if(MSVC)
    target_compile_options(lofibox_zero_host_lock PRIVATE /W4 /permissive- /FS)
else()
    target_compile_options(lofibox_zero_host_lock PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_library(lofibox_zero_host_runtime STATIC
    src/platform/host/acoustid_client.cpp
    src/audio/output/host_audio_playback_backend.cpp
    src/platform/host/artwork_provider.cpp
    src/platform/host/connectivity_provider.cpp
    src/platform/host/cover_art_archive_client.cpp
    src/platform/host/ffprobe_metadata_reader.cpp
    src/platform/host/host_runtime_service_providers.cpp
    src/platform/host/lyrics_provider.cpp
    src/platform/host/lyrics_pipeline_components.cpp
    src/platform/host/lyrics_source_clients.cpp
    src/platform/host/legacy_asset_loader.cpp
    src/platform/host/metadata_provider.cpp
    src/platform/host/musicbrainz_client.cpp
    src/platform/host/png_canvas_loader.cpp
    src/platform/host/remote_media_runtime.cpp
    src/platform/host/runtime_cache_store.cpp
    src/platform/host/runtime_enrichment_clients.cpp
    src/platform/host/runtime_host_internal.cpp
    src/platform/host/runtime_host_tools.cpp
    src/platform/host/runtime_logger.cpp
    src/platform/host/runtime_metadata_enrichment_orchestrator.cpp
    src/platform/host/runtime_process.cpp
    src/platform/host/runtime_remote_media_tool.cpp
    src/platform/host/runtime_script_bridge.cpp
    src/platform/host/runtime_services_factory.cpp
    src/platform/host/tag_writer.cpp
    src/platform/host/track_identity_provider.cpp
    src/platform/host/xdg_remote_profile_store.cpp
)
target_include_directories(lofibox_zero_host_runtime PUBLIC "${PROJECT_SOURCE_DIR}/src")
target_compile_features(lofibox_zero_host_runtime PUBLIC cxx_std_20)
target_link_libraries(lofibox_zero_host_runtime PUBLIC lofibox_zero_host_lock lofibox_zero_core Threads::Threads)
target_compile_definitions(lofibox_zero_host_runtime PRIVATE
    LOFIBOX_PROJECT_SOURCE_DIR="${PROJECT_SOURCE_DIR}"
    LOFIBOX_INSTALLED_HELPER_DIR="${CMAKE_INSTALL_PREFIX}/${LOFIBOX_PRIVATE_LIBDIR}"
)
if(MSVC)
    target_compile_options(lofibox_zero_host_runtime PRIVATE /W4 /permissive- /FS)
else()
    target_compile_options(lofibox_zero_host_runtime PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_library(lofibox_zero_target_cli STATIC
    src/cli/direct_cli.cpp
    src/cli/runtime_cli.cpp
    src/targets/cli_options.cpp
)
target_include_directories(lofibox_zero_target_cli PUBLIC "${PROJECT_SOURCE_DIR}/src")
target_compile_features(lofibox_zero_target_cli PUBLIC cxx_std_20)
target_link_libraries(lofibox_zero_target_cli PUBLIC lofibox_zero_core)
if(MSVC)
    target_compile_options(lofibox_zero_target_cli PRIVATE /W4 /permissive- /FS)
else()
    target_compile_options(lofibox_zero_target_cli PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(LOFIBOX_BUILD_WEBUI)
    add_library(lofibox_webui STATIC
        src/webui/webui_json.cpp
        src/webui/webui_config.cpp
        src/webui/webui_runtime_adapter.cpp
        src/webui/webui_projection.cpp
        src/webui/webui_static_assets.cpp
        src/webui/webui_http_router.cpp
        src/webui/webui_ws_runtime_stream.cpp
        src/webui/webui_server.cpp
    )
    target_include_directories(lofibox_webui PUBLIC "${PROJECT_SOURCE_DIR}/src")
    target_compile_features(lofibox_webui PUBLIC cxx_std_20)
    target_link_libraries(lofibox_webui PUBLIC lofibox_zero_core)
    if(MSVC)
        target_compile_options(lofibox_webui PRIVATE /W4 /permissive- /FS)
    else()
        target_compile_options(lofibox_webui PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endif()

if(LOFIBOX_BUILD_TUI)
    add_library(lofibox_zero_tui STATIC
        src/tui/tui_app.cpp
        src/tui/tui_model.cpp
        src/tui/tui_state.cpp
        src/tui/tui_view_mode.cpp
        src/tui/tui_renderer.cpp
        src/tui/tui_layout_engine.cpp
        src/tui/tui_input_router.cpp
        src/tui/tui_command_palette.cpp
        src/tui/widgets/tui_box.cpp
        src/tui/widgets/tui_text.cpp
        src/tui/widgets/tui_progress_bar.cpp
        src/tui/widgets/tui_spectrum.cpp
        src/tui/widgets/tui_vu_meter.cpp
        src/tui/widgets/tui_lyrics_view.cpp
        src/tui/widgets/tui_queue_view.cpp
        src/tui/widgets/tui_eq_view.cpp
        src/tui/widgets/tui_status_line.cpp
        src/tui/widgets/tui_help_overlay.cpp
        src/tui/pages/dashboard_tui_page.cpp
        src/tui/pages/now_playing_tui_page.cpp
        src/tui/pages/lyrics_tui_page.cpp
        src/tui/pages/spectrum_tui_page.cpp
        src/tui/pages/queue_tui_page.cpp
        src/tui/pages/library_tui_page.cpp
        src/tui/pages/sources_tui_page.cpp
        src/tui/pages/eq_tui_page.cpp
        src/tui/pages/dsp_tui_page.cpp
        src/tui/pages/diagnostics_tui_page.cpp
        src/tui/pages/creator_tui_page.cpp
        src/tui/pages/help_tui_page.cpp
        src/tui/pages/command_palette_tui_page.cpp
        src/platform/tui/terminal_presenter.cpp
        src/platform/tui/terminal_input.cpp
        src/platform/tui/terminal_capabilities.cpp
        src/platform/tui/ansi_escape.cpp
    )
    target_include_directories(lofibox_zero_tui PUBLIC "${PROJECT_SOURCE_DIR}/src")
    target_compile_features(lofibox_zero_tui PUBLIC cxx_std_20)
    target_link_libraries(lofibox_zero_tui PUBLIC lofibox_zero_core)
    if(MSVC)
        target_compile_options(lofibox_zero_tui PRIVATE /W4 /permissive- /FS)
    else()
        target_compile_options(lofibox_zero_tui PRIVATE -Wall -Wextra -Wpedantic)
    endif()

    add_executable(lofibox_zero_tui_bin
        src/targets/tui_main.cpp
    )
    set_target_properties(lofibox_zero_tui_bin PROPERTIES OUTPUT_NAME lofibox-tui)
    target_link_libraries(lofibox_zero_tui_bin PRIVATE lofibox_zero_tui)
    install(TARGETS lofibox_zero_tui_bin
        RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
    )
endif()

if(LOFIBOX_BUILD_X11)
    if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
        message(FATAL_ERROR "LOFIBOX_BUILD_X11 requires a Linux target environment.")
    endif()

    find_package(X11 REQUIRED)

    add_executable(lofibox_zero_x11
        src/platform/x11/x11_presenter.cpp
        src/targets/x11_main.cpp
    )
    set_target_properties(lofibox_zero_x11 PROPERTIES OUTPUT_NAME lofibox)
    target_link_libraries(lofibox_zero_x11 PRIVATE
        lofibox_zero_host_runtime
        lofibox_zero_target_cli
        X11::X11
    )
    if(TARGET lofibox_zero_tui)
        target_link_libraries(lofibox_zero_x11 PRIVATE lofibox_zero_tui)
        target_compile_definitions(lofibox_zero_x11 PRIVATE LOFIBOX_HAVE_TUI=1)
    endif()
    if(TARGET lofibox_webui)
        target_link_libraries(lofibox_zero_x11 PRIVATE lofibox_zero_core lofibox_webui)
        target_compile_definitions(lofibox_zero_x11 PRIVATE LOFIBOX_HAVE_WEBUI=1)
    endif()
    install(TARGETS lofibox_zero_x11
        RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
    )
endif()

if(LOFIBOX_BUILD_DEVICE)
    if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
        message(FATAL_ERROR "LOFIBOX_BUILD_DEVICE requires a Linux target environment.")
    endif()

    find_package(PkgConfig REQUIRED)
    pkg_check_modules(XKBCOMMON REQUIRED IMPORTED_TARGET xkbcommon)

    add_executable(lofibox_zero_device
        src/platform/device/linux_evdev_keyboard.cpp
        src/platform/device/linux_framebuffer.cpp
        src/platform/device/linux_framebuffer_platform.cpp
        src/targets/device_main.cpp
    )
    target_link_libraries(lofibox_zero_device PRIVATE
        lofibox_zero_host_runtime
        lofibox_zero_target_cli
        lofibox_zero_core
        PkgConfig::XKBCOMMON
    )
    if(TARGET lofibox_zero_tui)
        target_link_libraries(lofibox_zero_device PRIVATE lofibox_zero_tui)
        target_compile_definitions(lofibox_zero_device PRIVATE LOFIBOX_HAVE_TUI=1)
    endif()
    if(TARGET lofibox_webui)
        target_link_libraries(lofibox_zero_device PRIVATE lofibox_webui)
        target_compile_definitions(lofibox_zero_device PRIVATE LOFIBOX_HAVE_WEBUI=1)
    endif()
    install(TARGETS lofibox_zero_device
        RUNTIME DESTINATION "${LOFIBOX_PRIVATE_LIBDIR}"
    )
    if(NOT TARGET lofibox_zero_x11)
        install(PROGRAMS "$<TARGET_FILE:lofibox_zero_device>"
            DESTINATION "${CMAKE_INSTALL_BINDIR}"
            RENAME lofibox
        )
    endif()
endif()

install(FILES assets/README.md
    DESTINATION "${CMAKE_INSTALL_DATADIR}/lofibox/assets"
)

install(DIRECTORY assets/ui/icons/
    DESTINATION "${CMAKE_INSTALL_DATADIR}/lofibox/assets/ui/icons"
)

install(DIRECTORY data/plugins/
    DESTINATION "${CMAKE_INSTALL_DATADIR}/lofibox/plugins"
)

# Generated Python version module — single source of truth for Python helpers.
configure_file(
    "${PROJECT_SOURCE_DIR}/cmake/version.py.in"
    "${CMAKE_BINARY_DIR}/generated/version.py"
    @ONLY
)

file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/generated/man")
configure_file(
    "${PROJECT_SOURCE_DIR}/data/man/lofibox.1.in"
    "${CMAKE_BINARY_DIR}/generated/man/lofibox.1"
    @ONLY
)

install(FILES "${CMAKE_BINARY_DIR}/generated/version.py"
    DESTINATION "${LOFIBOX_PRIVATE_LIBDIR}"
)

install(FILES scripts/tag_writer.py scripts/remote_media_tool.py
    DESTINATION "${LOFIBOX_PRIVATE_LIBDIR}"
)

install(DIRECTORY src/remote/
    DESTINATION "${LOFIBOX_PRIVATE_LIBDIR}/remote"
    FILES_MATCHING
    PATTERN "*.py"
    PATTERN "__pycache__" EXCLUDE
)

install(FILES data/io.github.vicliu624.lofibox.desktop
    DESTINATION "${CMAKE_INSTALL_DATADIR}/applications"
)

if(TARGET lofibox_zero_device)
    install(PROGRAMS data/lofibox-cardputer-zero-applaunch
        DESTINATION "${LOFIBOX_PRIVATE_LIBDIR}"
        RENAME lofibox-applaunch
    )

    install(FILES data/lofibox-cardputer-zero.desktop
        DESTINATION "${CMAKE_INSTALL_DATADIR}/APPLaunch/applications"
        RENAME lofibox.desktop
    )

    install(FILES assets/ui/icons/cardputer-zero/lofibox.png
        DESTINATION "${CMAKE_INSTALL_DATADIR}/APPLaunch/share/images"
    )
endif()

install(FILES data/io.github.vicliu624.lofibox.metainfo.xml
    DESTINATION "${CMAKE_INSTALL_DATADIR}/metainfo"
)

install(FILES data/io.github.vicliu624.lofibox.svg
    DESTINATION "${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps"
)

install(FILES assets/ui/icons/legacy-lofibox/logo.png
    DESTINATION "${CMAKE_INSTALL_DATADIR}/icons/hicolor/180x180/apps"
    RENAME io.github.vicliu624.lofibox.png
)

install(FILES data/mime/io.github.vicliu624.lofibox.xml
    DESTINATION "${CMAKE_INSTALL_DATADIR}/mime/packages"
)

install(FILES "${CMAKE_BINARY_DIR}/generated/man/lofibox.1"
    DESTINATION "${CMAKE_INSTALL_MANDIR}/man1"
)

install(DIRECTORY docs/
    DESTINATION "${CMAKE_INSTALL_DOCDIR}"
)

install(FILES README.md
    DESTINATION "${CMAKE_INSTALL_DOCDIR}"
)

install(FILES LICENSE COPYING
    DESTINATION "${CMAKE_INSTALL_DOCDIR}"
)

if(BUILD_TESTING)
    add_executable(lofibox_app_smoke tests/hello_app_smoke.cpp)
    target_link_libraries(lofibox_app_smoke PRIVATE lofibox_zero_core)
    add_test(NAME lofibox_app_smoke COMMAND lofibox_app_smoke)

    add_executable(lofibox_app_command_executor_smoke tests/app_command_executor_smoke.cpp)
    target_link_libraries(lofibox_app_command_executor_smoke PRIVATE lofibox_zero_core)
    add_test(NAME lofibox_app_command_executor_smoke COMMAND lofibox_app_command_executor_smoke)

    add_executable(lofibox_application_command_boundary_smoke tests/application_command_boundary_smoke.cpp)
    target_link_libraries(lofibox_application_command_boundary_smoke PRIVATE lofibox_zero_core)
    add_test(NAME lofibox_application_command_boundary_smoke COMMAND lofibox_application_command_boundary_smoke)

    add_executable(lofibox_runtime_command_bus_smoke tests/runtime_command_bus_smoke.cpp)
    target_link_libraries(lofibox_runtime_command_bus_smoke PRIVATE lofibox_zero_core)
    add_test(NAME lofibox_runtime_command_bus_smoke COMMAND lofibox_runtime_command_bus_smoke)

    add_executable(lofibox_runtime_domains_smoke tests/runtime_domains_smoke.cpp)
    target_link_libraries(lofibox_runtime_domains_smoke PRIVATE lofibox_zero_core)
    add_test(NAME lofibox_runtime_domains_smoke COMMAND lofibox_runtime_domains_smoke)

    add_executable(lofibox_runtime_client_server_smoke tests/runtime_client_server_smoke.cpp)
    target_link_libraries(lofibox_runtime_client_server_smoke PRIVATE lofibox_zero_core)
    add_test(NAME lofibox_runtime_client_server_smoke COMMAND lofibox_runtime_client_server_smoke)

    add_executable(lofibox_runtime_host_smoke tests/runtime_host_smoke.cpp)
    target_link_libraries(lofibox_runtime_host_smoke PRIVATE lofibox_zero_core)
    add_test(NAME lofibox_runtime_host_smoke COMMAND lofibox_runtime_host_smoke)

    add_executable(lofibox_runtime_transport_socket_smoke tests/runtime_transport_socket_smoke.cpp)
    target_link_libraries(lofibox_runtime_transport_socket_smoke PRIVATE lofibox_zero_core lofibox_zero_target_cli)
    add_test(NAME lofibox_runtime_transport_socket_smoke COMMAND lofibox_runtime_transport_socket_smoke)

    add_executable(lofibox_runtime_event_stream_smoke tests/runtime_event_stream_smoke.cpp)
    target_link_libraries(lofibox_runtime_event_stream_smoke PRIVATE lofibox_zero_core)
    add_test(NAME lofibox_runtime_event_stream_smoke COMMAND lofibox_runtime_event_stream_smoke)

    add_executable(lofibox_runtime_cli_smoke tests/runtime_cli_smoke.cpp)
    target_link_libraries(lofibox_runtime_cli_smoke PRIVATE lofibox_zero_target_cli)
    add_test(NAME lofibox_runtime_cli_smoke COMMAND lofibox_runtime_cli_smoke)

    add_executable(lofibox_cli_options_smoke tests/cli_options_smoke.cpp)
    target_link_libraries(lofibox_cli_options_smoke PRIVATE lofibox_zero_target_cli)
    add_test(NAME lofibox_cli_options_smoke COMMAND lofibox_cli_options_smoke)

    if(TARGET lofibox_zero_tui)
        add_executable(lofibox_tui_progress_bar_smoke tests/tui_progress_bar_smoke.cpp)
        target_link_libraries(lofibox_tui_progress_bar_smoke PRIVATE lofibox_zero_tui)
        add_test(NAME lofibox_tui_progress_bar_smoke COMMAND lofibox_tui_progress_bar_smoke)

        add_executable(lofibox_tui_spectrum_renderer_smoke tests/tui_spectrum_renderer_smoke.cpp)
        target_link_libraries(lofibox_tui_spectrum_renderer_smoke PRIVATE lofibox_zero_tui)
        add_test(NAME lofibox_tui_spectrum_renderer_smoke COMMAND lofibox_tui_spectrum_renderer_smoke)

        add_executable(lofibox_tui_lyrics_view_smoke tests/tui_lyrics_view_smoke.cpp)
        target_link_libraries(lofibox_tui_lyrics_view_smoke PRIVATE lofibox_zero_tui)
        add_test(NAME lofibox_tui_lyrics_view_smoke COMMAND lofibox_tui_lyrics_view_smoke)

        add_executable(lofibox_tui_layout_engine_smoke tests/tui_layout_engine_smoke.cpp)
        target_link_libraries(lofibox_tui_layout_engine_smoke PRIVATE lofibox_zero_tui)
        add_test(NAME lofibox_tui_layout_engine_smoke COMMAND lofibox_tui_layout_engine_smoke)

        add_executable(lofibox_tui_input_router_smoke tests/tui_input_router_smoke.cpp)
        target_link_libraries(lofibox_tui_input_router_smoke PRIVATE lofibox_zero_tui)
        add_test(NAME lofibox_tui_input_router_smoke COMMAND lofibox_tui_input_router_smoke)

        add_executable(lofibox_tui_dashboard_render_smoke tests/tui_dashboard_render_smoke.cpp)
        target_link_libraries(lofibox_tui_dashboard_render_smoke PRIVATE lofibox_zero_tui)
        add_test(NAME lofibox_tui_dashboard_render_smoke COMMAND lofibox_tui_dashboard_render_smoke)

        add_executable(lofibox_tui_runtime_projection_smoke tests/tui_runtime_projection_smoke.cpp)
        target_link_libraries(lofibox_tui_runtime_projection_smoke PRIVATE lofibox_zero_tui)
        add_test(NAME lofibox_tui_runtime_projection_smoke COMMAND lofibox_tui_runtime_projection_smoke)

        add_executable(lofibox_tui_terminal_capabilities_smoke tests/tui_terminal_capabilities_smoke.cpp)
        target_link_libraries(lofibox_tui_terminal_capabilities_smoke PRIVATE lofibox_zero_tui)
        add_test(NAME lofibox_tui_terminal_capabilities_smoke COMMAND lofibox_tui_terminal_capabilities_smoke)
    endif()

    add_executable(lofibox_runtime_seek_stop_queueclear_smoke tests/runtime_seek_stop_queueclear_smoke.cpp)
    target_link_libraries(lofibox_runtime_seek_stop_queueclear_smoke PRIVATE lofibox_zero_core)
    add_test(NAME lofibox_runtime_seek_stop_queueclear_smoke COMMAND lofibox_runtime_seek_stop_queueclear_smoke)

    add_executable(lofibox_runtime_settings_apply_smoke tests/runtime_settings_apply_smoke.cpp)
    target_link_libraries(lofibox_runtime_settings_apply_smoke PRIVATE lofibox_zero_core)
    add_test(NAME lofibox_runtime_settings_apply_smoke COMMAND lofibox_runtime_settings_apply_smoke)

    add_executable(lofibox_runtime_shutdown_reload_smoke tests/runtime_shutdown_reload_smoke.cpp)
    target_link_libraries(lofibox_runtime_shutdown_reload_smoke PRIVATE lofibox_zero_core)
    add_test(NAME lofibox_runtime_shutdown_reload_smoke COMMAND lofibox_runtime_shutdown_reload_smoke)

    add_executable(lofibox_desktop_open_runtime_command_smoke tests/desktop_open_runtime_command_smoke.cpp)
    target_link_libraries(lofibox_desktop_open_runtime_command_smoke PRIVATE lofibox_zero_core)
    add_test(NAME lofibox_desktop_open_runtime_command_smoke COMMAND lofibox_desktop_open_runtime_command_smoke)

    add_executable(lofibox_remote_browse_query_service_smoke tests/remote_browse_query_service_smoke.cpp)
    target_link_libraries(lofibox_remote_browse_query_service_smoke PRIVATE lofibox_zero_core)
    add_test(NAME lofibox_remote_browse_query_service_smoke COMMAND lofibox_remote_browse_query_service_smoke)

    add_executable(lofibox_plugin_discovery_smoke tests/plugin_discovery_smoke.cpp)
    target_link_libraries(lofibox_plugin_discovery_smoke PRIVATE lofibox_zero_core)
    add_test(NAME lofibox_plugin_discovery_smoke COMMAND lofibox_plugin_discovery_smoke)

    add_executable(lofibox_plugin_runtime_smoke tests/plugin_runtime_smoke.cpp)
    target_link_libraries(lofibox_plugin_runtime_smoke PRIVATE lofibox_zero_core)
    add_test(NAME lofibox_plugin_runtime_smoke COMMAND lofibox_plugin_runtime_smoke)

    add_executable(lofibox_direct_cli_smoke tests/direct_cli_smoke.cpp)
    target_link_libraries(lofibox_direct_cli_smoke PRIVATE lofibox_zero_target_cli)
    add_test(NAME lofibox_direct_cli_smoke COMMAND lofibox_direct_cli_smoke)

    add_executable(lofibox_app_input_router_smoke tests/app_input_router_smoke.cpp)
    target_link_libraries(lofibox_app_input_router_smoke PRIVATE lofibox_zero_core)
    add_test(NAME lofibox_app_input_router_smoke COMMAND lofibox_app_input_router_smoke)

    add_executable(lofibox_text_input_utils_smoke tests/text_input_utils_smoke.cpp)
    target_link_libraries(lofibox_text_input_utils_smoke PRIVATE lofibox_zero_core)
    add_test(NAME lofibox_text_input_utils_smoke COMMAND lofibox_text_input_utils_smoke)

    add_executable(lofibox_app_lifecycle_smoke tests/app_lifecycle_smoke.cpp)
    target_link_libraries(lofibox_app_lifecycle_smoke PRIVATE lofibox_zero_core)
    add_test(NAME lofibox_app_lifecycle_smoke COMMAND lofibox_app_lifecycle_smoke)

    add_executable(lofibox_app_page_model_smoke tests/app_page_model_smoke.cpp)
    target_link_libraries(lofibox_app_page_model_smoke PRIVATE lofibox_zero_core)
    add_test(NAME lofibox_app_page_model_smoke COMMAND lofibox_app_page_model_smoke)

    add_executable(lofibox_library_scan_smoke tests/library_scan_smoke.cpp)
    target_link_libraries(lofibox_library_scan_smoke PRIVATE lofibox_zero_core)
    add_test(NAME lofibox_library_scan_smoke COMMAND lofibox_library_scan_smoke)

    add_executable(lofibox_local_root_source_profile_smoke tests/local_root_source_profile_smoke.cpp)
    target_link_libraries(lofibox_local_root_source_profile_smoke PRIVATE lofibox_zero_core)
    add_test(NAME lofibox_local_root_source_profile_smoke COMMAND lofibox_local_root_source_profile_smoke)

    add_executable(lofibox_library_controller_smoke tests/library_controller_smoke.cpp)
    target_link_libraries(lofibox_library_controller_smoke PRIVATE lofibox_zero_core)
    add_test(NAME lofibox_library_controller_smoke COMMAND lofibox_library_controller_smoke)

    add_executable(lofibox_playback_controller_smoke tests/playback_controller_smoke.cpp)
    target_link_libraries(lofibox_playback_controller_smoke PRIVATE lofibox_zero_core)
    add_test(NAME lofibox_playback_controller_smoke COMMAND lofibox_playback_controller_smoke)

    add_executable(lofibox_metadata_merge_policy_smoke tests/metadata_merge_policy_smoke.cpp)
    target_link_libraries(lofibox_metadata_merge_policy_smoke PRIVATE lofibox_zero_core)
    add_test(NAME lofibox_metadata_merge_policy_smoke COMMAND lofibox_metadata_merge_policy_smoke)

    add_executable(lofibox_enrichment_authority_policy_smoke tests/enrichment_authority_policy_smoke.cpp)
    target_link_libraries(lofibox_enrichment_authority_policy_smoke PRIVATE lofibox_zero_core)
    add_test(NAME lofibox_enrichment_authority_policy_smoke COMMAND lofibox_enrichment_authority_policy_smoke)

    add_executable(lofibox_library_repository_smoke tests/library_repository_smoke.cpp)
    target_link_libraries(lofibox_library_repository_smoke PRIVATE lofibox_zero_core)
    add_test(NAME lofibox_library_repository_smoke COMMAND lofibox_library_repository_smoke)

    add_executable(lofibox_library_store_smoke tests/library_store_smoke.cpp)
    target_link_libraries(lofibox_library_store_smoke PRIVATE lofibox_zero_core)
    add_test(NAME lofibox_library_store_smoke COMMAND lofibox_library_store_smoke)

    add_executable(lofibox_media_search_queue_smoke tests/media_search_queue_smoke.cpp)
    target_link_libraries(lofibox_media_search_queue_smoke PRIVATE lofibox_zero_core)
    add_test(NAME lofibox_media_search_queue_smoke COMMAND lofibox_media_search_queue_smoke)

    add_executable(lofibox_audio_pipeline_controller_smoke tests/audio_pipeline_controller_smoke.cpp)
    target_link_libraries(lofibox_audio_pipeline_controller_smoke PRIVATE lofibox_zero_core)
    add_test(NAME lofibox_audio_pipeline_controller_smoke COMMAND lofibox_audio_pipeline_controller_smoke)

    add_executable(lofibox_audio_child_lifecycle_smoke tests/audio_child_lifecycle_smoke.cpp)
    target_link_libraries(lofibox_audio_child_lifecycle_smoke PRIVATE lofibox_zero_host_runtime)
    add_test(NAME lofibox_audio_child_lifecycle_smoke COMMAND lofibox_audio_child_lifecycle_smoke)

    add_executable(lofibox_host_audio_realtime_dsp_smoke tests/host_audio_realtime_dsp_smoke.cpp)
    target_link_libraries(lofibox_host_audio_realtime_dsp_smoke PRIVATE lofibox_zero_host_runtime)
    add_test(NAME lofibox_host_audio_realtime_dsp_smoke COMMAND lofibox_host_audio_realtime_dsp_smoke)

    add_executable(lofibox_equalizer_smoke tests/equalizer_smoke.cpp)
    target_link_libraries(lofibox_equalizer_smoke PRIVATE lofibox_zero_core)
    add_test(NAME lofibox_equalizer_smoke COMMAND lofibox_equalizer_smoke)

    add_executable(lofibox_dsp_chain_smoke tests/dsp_chain_smoke.cpp)
    target_link_libraries(lofibox_dsp_chain_smoke PRIVATE lofibox_zero_core)
    add_test(NAME lofibox_dsp_chain_smoke COMMAND lofibox_dsp_chain_smoke)

    add_executable(lofibox_realtime_dsp_engine_smoke tests/realtime_dsp_engine_smoke.cpp)
    target_link_libraries(lofibox_realtime_dsp_engine_smoke PRIVATE lofibox_zero_core)
    add_test(NAME lofibox_realtime_dsp_engine_smoke COMMAND lofibox_realtime_dsp_engine_smoke)

    add_executable(lofibox_dsp_profile_manager_smoke tests/dsp_profile_manager_smoke.cpp)
    target_link_libraries(lofibox_dsp_profile_manager_smoke PRIVATE lofibox_zero_core)
    add_test(NAME lofibox_dsp_profile_manager_smoke COMMAND lofibox_dsp_profile_manager_smoke)

    add_executable(lofibox_cache_manager_smoke tests/cache_manager_smoke.cpp)
    target_link_libraries(lofibox_cache_manager_smoke PRIVATE lofibox_zero_core)
    add_test(NAME lofibox_cache_manager_smoke COMMAND lofibox_cache_manager_smoke)

    add_executable(lofibox_streaming_completion_smoke tests/streaming_completion_smoke.cpp)
    target_link_libraries(lofibox_streaming_completion_smoke PRIVATE lofibox_zero_core)
    add_test(NAME lofibox_streaming_completion_smoke COMMAND lofibox_streaming_completion_smoke)

    add_executable(lofibox_remote_browse_playback_flow_smoke tests/remote_browse_playback_flow_smoke.cpp)
    target_link_libraries(lofibox_remote_browse_playback_flow_smoke PRIVATE lofibox_zero_core)
    add_test(NAME lofibox_remote_browse_playback_flow_smoke COMMAND lofibox_remote_browse_playback_flow_smoke)

    add_executable(lofibox_library_governance_smoke tests/library_governance_smoke.cpp)
    target_link_libraries(lofibox_library_governance_smoke PRIVATE lofibox_zero_core)
    add_test(NAME lofibox_library_governance_smoke COMMAND lofibox_library_governance_smoke)

    add_executable(lofibox_metadata_governance_smoke tests/metadata_governance_smoke.cpp)
    target_link_libraries(lofibox_metadata_governance_smoke PRIVATE lofibox_zero_core)
    add_test(NAME lofibox_metadata_governance_smoke COMMAND lofibox_metadata_governance_smoke)

    add_executable(lofibox_playback_stability_smoke tests/playback_stability_smoke.cpp)
    target_link_libraries(lofibox_playback_stability_smoke PRIVATE lofibox_zero_core)
    add_test(NAME lofibox_playback_stability_smoke COMMAND lofibox_playback_stability_smoke)

    add_executable(lofibox_desktop_security_completion_smoke tests/desktop_security_completion_smoke.cpp)
    target_link_libraries(lofibox_desktop_security_completion_smoke PRIVATE lofibox_zero_core)
    add_test(NAME lofibox_desktop_security_completion_smoke COMMAND lofibox_desktop_security_completion_smoke)

    add_executable(lofibox_cardputer_applaunch_metadata_smoke tests/cardputer_applaunch_metadata_smoke.cpp)
    target_compile_definitions(lofibox_cardputer_applaunch_metadata_smoke PRIVATE LOFIBOX_PROJECT_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
    add_test(NAME lofibox_cardputer_applaunch_metadata_smoke COMMAND lofibox_cardputer_applaunch_metadata_smoke)

    if(LOFIBOX_BUILD_DEVICE)
        add_executable(lofibox_linux_evdev_keyboard_smoke
            tests/linux_evdev_keyboard_smoke.cpp
            src/platform/device/linux_evdev_keyboard.cpp
        )
        target_link_libraries(lofibox_linux_evdev_keyboard_smoke PRIVATE lofibox_zero_core PkgConfig::XKBCOMMON)
        add_test(NAME lofibox_linux_evdev_keyboard_smoke COMMAND lofibox_linux_evdev_keyboard_smoke)
    endif()

    add_executable(lofibox_packaging_version_metadata_smoke tests/packaging_version_metadata_smoke.cpp)
    target_compile_definitions(lofibox_packaging_version_metadata_smoke PRIVATE
        LOFIBOX_PROJECT_SOURCE_DIR="${PROJECT_SOURCE_DIR}"
        LOFIBOX_PROJECT_VERSION="${PROJECT_VERSION}"
    )
    add_test(NAME lofibox_packaging_version_metadata_smoke COMMAND lofibox_packaging_version_metadata_smoke)

    add_executable(lofibox_final_surface_pages_smoke tests/final_surface_pages_smoke.cpp)
    target_link_libraries(lofibox_final_surface_pages_smoke PRIVATE lofibox_zero_core)
    add_test(NAME lofibox_final_surface_pages_smoke COMMAND lofibox_final_surface_pages_smoke)

    add_executable(lofibox_main_menu_visual_smoke tests/main_menu_visual_smoke.cpp)
    target_link_libraries(lofibox_main_menu_visual_smoke PRIVATE lofibox_zero_host_runtime)
    target_compile_definitions(lofibox_main_menu_visual_smoke PRIVATE LOFIBOX_ASSET_DIR="${PROJECT_SOURCE_DIR}/assets")
    add_test(NAME lofibox_main_menu_visual_smoke COMMAND lofibox_main_menu_visual_smoke)

    add_executable(lofibox_logo_asset_transparency_smoke tests/logo_asset_transparency_smoke.cpp)
    target_link_libraries(lofibox_logo_asset_transparency_smoke PRIVATE lofibox_zero_host_runtime)
    target_compile_definitions(lofibox_logo_asset_transparency_smoke PRIVATE LOFIBOX_ASSET_DIR="${PROJECT_SOURCE_DIR}/assets")
    add_test(NAME lofibox_logo_asset_transparency_smoke COMMAND lofibox_logo_asset_transparency_smoke)

    add_executable(lofibox_main_menu_shortcuts_smoke tests/main_menu_shortcuts_smoke.cpp)
    target_link_libraries(lofibox_main_menu_shortcuts_smoke PRIVATE lofibox_zero_core)
    add_test(NAME lofibox_main_menu_shortcuts_smoke COMMAND lofibox_main_menu_shortcuts_smoke)

    add_executable(lofibox_list_focus_visual_smoke tests/list_focus_visual_smoke.cpp)
    target_link_libraries(lofibox_list_focus_visual_smoke PRIVATE lofibox_zero_core)
    add_test(NAME lofibox_list_focus_visual_smoke COMMAND lofibox_list_focus_visual_smoke)

    add_executable(lofibox_list_help_modal_smoke tests/list_help_modal_smoke.cpp)
    target_link_libraries(lofibox_list_help_modal_smoke PRIVATE lofibox_zero_core)
    add_test(NAME lofibox_list_help_modal_smoke COMMAND lofibox_list_help_modal_smoke)

    add_executable(lofibox_network_settings_smoke tests/network_settings_smoke.cpp)
    target_link_libraries(lofibox_network_settings_smoke PRIVATE lofibox_zero_core)
    add_test(NAME lofibox_network_settings_smoke COMMAND lofibox_network_settings_smoke)

    add_executable(lofibox_projection_desktop_boundary_smoke tests/projection_desktop_boundary_smoke.cpp)
    target_link_libraries(lofibox_projection_desktop_boundary_smoke PRIVATE lofibox_zero_core)
    add_test(NAME lofibox_projection_desktop_boundary_smoke COMMAND lofibox_projection_desktop_boundary_smoke)

    add_executable(lofibox_source_manager_projection_smoke tests/source_manager_projection_smoke.cpp)
    target_link_libraries(lofibox_source_manager_projection_smoke PRIVATE lofibox_zero_core)
    add_test(NAME lofibox_source_manager_projection_smoke COMMAND lofibox_source_manager_projection_smoke)

    add_executable(lofibox_embedded_metadata_smoke tests/embedded_metadata_smoke.cpp)
    target_link_libraries(lofibox_embedded_metadata_smoke PRIVATE lofibox_zero_host_runtime)
    add_test(NAME lofibox_embedded_metadata_smoke COMMAND lofibox_embedded_metadata_smoke)

    add_executable(lofibox_embedded_artwork_smoke tests/embedded_artwork_smoke.cpp)
    target_link_libraries(lofibox_embedded_artwork_smoke PRIVATE lofibox_zero_host_runtime)
    add_test(NAME lofibox_embedded_artwork_smoke COMMAND lofibox_embedded_artwork_smoke)

    add_executable(lofibox_now_playing_artwork_smoke tests/now_playing_artwork_smoke.cpp)
    target_link_libraries(lofibox_now_playing_artwork_smoke PRIVATE lofibox_zero_host_runtime)
    add_test(NAME lofibox_now_playing_artwork_smoke COMMAND lofibox_now_playing_artwork_smoke)

    add_executable(lofibox_now_playing_tidal_spectrum_smoke tests/now_playing_tidal_spectrum_smoke.cpp)
    target_link_libraries(lofibox_now_playing_tidal_spectrum_smoke PRIVATE lofibox_zero_core)
    add_test(NAME lofibox_now_playing_tidal_spectrum_smoke COMMAND lofibox_now_playing_tidal_spectrum_smoke)

    add_executable(lofibox_lyrics_page_smoke tests/lyrics_page_smoke.cpp)
    target_link_libraries(lofibox_lyrics_page_smoke PRIVATE lofibox_zero_core)
    add_test(NAME lofibox_lyrics_page_smoke COMMAND lofibox_lyrics_page_smoke)

    add_executable(lofibox_root_directory_artwork_guard_smoke tests/root_directory_artwork_guard_smoke.cpp)
    target_link_libraries(lofibox_root_directory_artwork_guard_smoke PRIVATE lofibox_zero_host_runtime)
    add_test(NAME lofibox_root_directory_artwork_guard_smoke COMMAND lofibox_root_directory_artwork_guard_smoke)

    add_executable(lofibox_bitmap_font_utf8_smoke tests/bitmap_font_utf8_smoke.cpp)
    target_link_libraries(lofibox_bitmap_font_utf8_smoke PRIVATE lofibox_zero_core)
    add_test(NAME lofibox_bitmap_font_utf8_smoke COMMAND lofibox_bitmap_font_utf8_smoke)

    add_executable(lofibox_tag_writeback_smoke tests/tag_writeback_smoke.cpp)
    target_link_libraries(lofibox_tag_writeback_smoke PRIVATE lofibox_zero_host_runtime)
    add_test(NAME lofibox_tag_writeback_smoke COMMAND lofibox_tag_writeback_smoke)

    add_executable(lofibox_lyrics_writeback_policy_smoke tests/lyrics_writeback_policy_smoke.cpp)
    target_link_libraries(lofibox_lyrics_writeback_policy_smoke PRIVATE lofibox_zero_host_runtime)
    add_test(NAME lofibox_lyrics_writeback_policy_smoke COMMAND lofibox_lyrics_writeback_policy_smoke)

    add_executable(lofibox_single_instance_lock_smoke tests/single_instance_lock_smoke.cpp)
    target_link_libraries(lofibox_single_instance_lock_smoke PRIVATE lofibox_zero_host_lock)
    add_test(NAME lofibox_single_instance_lock_smoke COMMAND lofibox_single_instance_lock_smoke)
    set_tests_properties(lofibox_single_instance_lock_smoke PROPERTIES TIMEOUT 10)

    add_executable(lofibox_remote_media_smoke tests/remote_media_smoke.cpp)
    target_link_libraries(lofibox_remote_media_smoke PRIVATE lofibox_zero_host_runtime)
    add_test(NAME lofibox_remote_media_smoke COMMAND lofibox_remote_media_smoke)

    add_executable(lofibox_real_emby_integration_smoke tests/real_emby_integration_smoke.cpp)
    target_link_libraries(lofibox_real_emby_integration_smoke PRIVATE lofibox_zero_host_runtime)
    add_test(NAME lofibox_real_emby_integration_smoke COMMAND lofibox_real_emby_integration_smoke)

    add_executable(lofibox_real_remote_media_integration_smoke tests/real_remote_media_integration_smoke.cpp)
    target_link_libraries(lofibox_real_remote_media_integration_smoke PRIVATE lofibox_zero_host_runtime)
    add_test(NAME lofibox_real_remote_media_integration_smoke COMMAND lofibox_real_remote_media_integration_smoke)

    add_executable(lofibox_real_remote_lab_integration_smoke tests/real_remote_lab_integration_smoke.cpp)
    target_link_libraries(lofibox_real_remote_lab_integration_smoke PRIVATE lofibox_zero_host_runtime)
    add_test(NAME lofibox_real_remote_lab_integration_smoke COMMAND lofibox_real_remote_lab_integration_smoke)

    add_executable(lofibox_remote_source_security_smoke tests/remote_source_security_smoke.cpp)
    target_link_libraries(lofibox_remote_source_security_smoke PRIVATE lofibox_zero_core)
    add_test(NAME lofibox_remote_source_security_smoke COMMAND lofibox_remote_source_security_smoke)

    add_executable(lofibox_remote_profile_store_smoke tests/remote_profile_store_smoke.cpp)
    target_link_libraries(lofibox_remote_profile_store_smoke PRIVATE lofibox_zero_host_runtime)
    add_test(NAME lofibox_remote_profile_store_smoke COMMAND lofibox_remote_profile_store_smoke)

    add_executable(lofibox_runtime_service_provider_smoke tests/runtime_service_provider_smoke.cpp)
    target_link_libraries(lofibox_runtime_service_provider_smoke PRIVATE lofibox_zero_host_runtime)
    add_test(NAME lofibox_runtime_service_provider_smoke COMMAND lofibox_runtime_service_provider_smoke)

    add_executable(lofibox_filename_metadata_guess_smoke tests/filename_metadata_guess_smoke.cpp)
    target_link_libraries(lofibox_filename_metadata_guess_smoke PRIVATE lofibox_zero_host_runtime)
    add_test(NAME lofibox_filename_metadata_guess_smoke COMMAND lofibox_filename_metadata_guess_smoke)

    add_executable(lofibox_lrclib_search_fallback_smoke tests/lrclib_search_fallback_smoke.cpp)
    target_link_libraries(lofibox_lrclib_search_fallback_smoke PRIVATE lofibox_zero_host_runtime)
    add_test(NAME lofibox_lrclib_search_fallback_smoke COMMAND lofibox_lrclib_search_fallback_smoke)

    add_executable(lofibox_xdg_paths_smoke tests/xdg_paths_smoke.cpp)
    target_link_libraries(lofibox_xdg_paths_smoke PRIVATE lofibox_zero_host_runtime)
    add_test(NAME lofibox_xdg_paths_smoke COMMAND lofibox_xdg_paths_smoke)
    set_tests_properties(lofibox_xdg_paths_smoke PROPERTIES
        ENVIRONMENT "XDG_CONFIG_HOME=/tmp/lofibox-xdg-config;XDG_DATA_HOME=/tmp/lofibox-xdg-data;XDG_CACHE_HOME=/tmp/lofibox-xdg-cache;XDG_STATE_HOME=/tmp/lofibox-xdg-state;XDG_RUNTIME_DIR=/tmp/lofibox-xdg-runtime"
    )

    add_executable(lofibox_helper_script_path_smoke tests/helper_script_path_smoke.cpp)
    target_link_libraries(lofibox_helper_script_path_smoke PRIVATE lofibox_zero_host_runtime)
    add_test(NAME lofibox_helper_script_path_smoke COMMAND lofibox_helper_script_path_smoke)

    if(TARGET lofibox_webui)
        add_executable(lofibox_webui_smoke tests/webui_smoke.cpp)
        target_link_libraries(lofibox_webui_smoke PRIVATE lofibox_webui)
        add_test(NAME lofibox_webui_smoke COMMAND lofibox_webui_smoke)
    endif()
  endif()
