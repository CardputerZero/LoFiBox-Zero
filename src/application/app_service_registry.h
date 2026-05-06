// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "app/app_controller_set.h"
#include "app/runtime_services.h"
#include "application/cache_command_service.h"
#include "application/credential_command_service.h"
#include "application/library_mutation_service.h"
#include "application/library_open_action_service.h"
#include "application/library_query_service.h"
#include "application/playback_command_service.h"
#include "application/playback_status_query_service.h"
#include "application/queue_command_service.h"
#include "application/remote_browse_query_service.h"
#include "application/runtime_diagnostic_service.h"
#include "application/source_profile_command_service.h"

namespace lofibox::application {

class AppServiceRegistry {
public:
    AppServiceRegistry(app::AppControllerSet& controllers, app::RuntimeServices& runtime_services) noexcept;

    [[nodiscard]] PlaybackCommandService playbackCommands() const noexcept;
    [[nodiscard]] QueueCommandService queueCommands() const noexcept;
    [[nodiscard]] PlaybackStatusQueryService playbackStatus() const noexcept;
    [[nodiscard]] LibraryQueryService libraryQueries() const noexcept;
    [[nodiscard]] LibraryMutationService libraryMutations() const noexcept;
    [[nodiscard]] LibraryOpenActionService libraryOpenActions() const noexcept;
    [[nodiscard]] SourceProfileCommandService sourceProfiles() const noexcept;
    [[nodiscard]] RemoteBrowseQueryService remoteBrowseQueries() const noexcept;
    [[nodiscard]] CredentialCommandService credentials() const noexcept;
    [[nodiscard]] CacheCommandService cacheCommands() const noexcept;
    [[nodiscard]] RuntimeDiagnosticService diagnostics() const noexcept;
    [[nodiscard]] app::LibraryController& libraryController() noexcept { return controllers_.library; }

private:
    app::AppControllerSet& controllers_;
    app::RuntimeServices& runtime_services_;
};

} // namespace lofibox::application
