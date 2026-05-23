# SPDX-License-Identifier: GPL-3.0-or-later

param(
    [switch]$Quiet
)

$ErrorActionPreference = "Stop"

$repo = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path

function Convert-ToRepoPath([string]$Path)
{
    return $Path.Substring($repo.Length + 1).Replace("\", "/")
}

function Is-SourceFile([System.IO.FileInfo]$File)
{
    return @(".c", ".cc", ".cpp", ".h", ".hpp") -contains $File.Extension
}

function Test-AnyPrefix([string]$Value, [string[]]$Prefixes)
{
    foreach ($prefix in $Prefixes) {
        if ($Value.StartsWith($prefix, [System.StringComparison]::Ordinal)) {
            return $true
        }
    }
    return $false
}

function Add-Violation([System.Collections.Generic.List[string]]$Violations, [string]$Path, [int]$Line, [string]$Include, [string]$Reason)
{
    $Violations.Add("${Path}:${Line}: ${Include} - ${Reason}")
}

$includeRegex = '^\s*#\s*include\s+[<"]([^>"]+)[>"]'
$violations = [System.Collections.Generic.List[string]]::new()

Get-ChildItem -Path (Join-Path $repo "src") -Recurse -File | Where-Object { Is-SourceFile $_ } | ForEach-Object {
    $repoPath = Convert-ToRepoPath $_.FullName
    $lines = Get-Content -LiteralPath $_.FullName
    $currentFunction = ""

    for ($index = 0; $index -lt $lines.Count; $index++) {
        $line = $lines[$index]
        $lineNumber = $index + 1

        if ($repoPath -eq "src/app/app_runtime_context.cpp" -and $line -match 'AppRuntimeContext::([A-Za-z0-9_]+)\s*\(') {
            $currentFunction = $Matches[1]
        }

        if ($repoPath.StartsWith("src/ui/", [System.StringComparison]::Ordinal) -and $line -match '\bapp::') {
            Add-Violation $violations $repoPath $lineNumber "app::" "UI code must use UI projection types and must not reference app namespace types"
        }

        if ($repoPath -eq "src/app/lofibox_app.cpp") {
            if ($line -match 'case\s+\d+\s*:|selected\s*==\s*\d+|std::clamp|\.startTrack\s*\(|\.openSelectedListItem\s*\(|\.setShuffleEnabled\s*\(|\.setRepeatOne\s*\(|\.setRepeatAll\s*\(|\.cycleSongSortMode\s*\(') {
                Add-Violation $violations $repoPath $lineNumber "command semantics" "LoFiBoxApp must delegate page command semantics to app_command_executor instead of owning menu/list/playback/EQ branching"
            }
        }

        if ($repoPath -eq "src/app/app_runtime_context.h") {
            if ($line -match '^\s*(std::vector<std::filesystem::path>|ui::UiAssets|LibraryController|PlaybackController|NavigationState|SettingsState|NetworkState|MetadataServiceState|EqState)\s+[A-Za-z0-9_]+\{') {
                Add-Violation $violations $repoPath $lineNumber "gui shell ownership" "AppRuntimeContext must not re-accumulate raw GUI state, runtime services, or controllers; use AppRuntimeState plus AppServiceHost/RuntimeCommandClient references"
            }
            if ($line -match 'RuntimeServices|AppControllerSet|RuntimeHost|RuntimeSessionFacade|RuntimeCommandBus|RuntimeCommandServer|InProcessRuntimeCommandClient|UnixSocketRuntime') {
                Add-Violation $violations $repoPath $lineNumber "runtime host ownership" "AppRuntimeContext must not own or include runtime host internals, controllers, runtime services, or transports"
            }
        }

        if ($repoPath -eq "src/app/app_command_executor.cpp" -or $repoPath -eq "src/app/app_command_executor.h") {
            if ($line -match 'libraryController\s*\(|playbackController\s*\(|#\s*include\s+"app/library_controller\.h"|#\s*include\s+"playback/playback_controller\.h"') {
                Add-Violation $violations $repoPath $lineNumber "application command boundary" "GUI command routing must dispatch through AppServiceRegistry instead of exposing LibraryController or PlaybackController"
            }
            if ($line -match 'RuntimeCommandBus|RuntimeSessionFacade|runtime_command_bus\.h|runtime_session_facade\.h') {
                Add-Violation $violations $repoPath $lineNumber "runtime client boundary" "GUI command routing may emit runtime commands through the command target; it must not construct or depend on the runtime bus or session facade"
            }
        }

        if ($repoPath.StartsWith("src/application/", [System.StringComparison]::Ordinal)) {
            if ($line -match '\bAppRuntimeContext\b|ui::|pages::') {
                Add-Violation $violations $repoPath $lineNumber "application service boundary" "Application command/query services must not depend on GUI runtime context or UI pages"
            }
        }

        if ($repoPath -eq "src/app/app_runtime_context.cpp") {
            if ($line -match 'app_host_\.controllers\s*\(|app_host_\.services\s*\(') {
                Add-Violation $violations $repoPath $lineNumber "app host internals" "AppRuntimeContext must consume AppServiceRegistry and RuntimeCommandClient, not AppServiceHost controllers/services internals"
            }
            if ($line -match 'playbackCommands\(\)\.startRemoteStream\s*\(' -and $currentFunction -ne "startSelectedRemoteStream") {
                Add-Violation $violations $repoPath $lineNumber "runtime command bus bypass" "Remote stream playback from GUI/desktop/search/stream-detail flows must submit a RuntimeCommand; direct startRemoteStream is only allowed inside the runtime facade callback"
            }
            if ($currentFunction -eq "submitRuntimeCommand" -and $line -match 'runtime_bus_') {
                Add-Violation $violations $repoPath $lineNumber "runtime client boundary" "AppRuntimeContext::submitRuntimeCommand must use the in-process RuntimeCommandClient instead of dispatching the RuntimeCommandBus directly"
            }
            if ($line -match 'runtime_bus_\.snapshot\s*\(') {
                Add-Violation $violations $repoPath $lineNumber "runtime client boundary" "GUI page projections must query runtime snapshots through RuntimeCommandClient, not directly through RuntimeCommandBus"
            }
        }

        if ($repoPath.StartsWith("src/runtime/", [System.StringComparison]::Ordinal)) {
            if ($line -match '\bAppRuntimeContext\b|ui::|pages::|argv|argc') {
                Add-Violation $violations $repoPath $lineNumber "runtime command boundary" "Runtime command/session code must stay transport-neutral and must not depend on GUI runtime context, UI pages, or CLI argv parsing"
            }
        }

        if ($repoPath.StartsWith("src/cli/", [System.StringComparison]::Ordinal)) {
            if ($line -match '\bAppRuntimeContext\b|ui::|pages::|playbackCommands\(\)\.(pause|resume|toggle|step|start|setDspProfile)|queueCommands\(\)\.(step|rebuild|set)|RuntimeCommandBus|RuntimeCommandServer|RuntimeSessionFacade') {
                Add-Violation $violations $repoPath $lineNumber "cli boundary" "CLI may parse/format direct commands or use RuntimeCommandClient transport, but it must not touch GUI runtime, UI pages, controllers, runtime domains, bus, or server"
            }
        }

        if ($repoPath.StartsWith("src/tui/", [System.StringComparison]::Ordinal)) {
            if ($line -match '\bAppRuntimeContext\b|AudioBackend|PlaybackBackendController|LyricsProvider|LibraryScanner|AudioDecoder|CredentialCommandService|SourceProfileCommandService') {
                Add-Violation $violations $repoPath $lineNumber "tui boundary" "TUI must project runtime state and emit runtime commands; it must not call GUI runtime, audio backend, lyrics provider, scanner, decoder, or credential/source services directly"
            }
        }

        if ($repoPath.StartsWith("src/playback/", [System.StringComparison]::Ordinal)) {
            if ($line -match '#\s*include\s+"application/') {
                Add-Violation $violations $repoPath $lineNumber "playback internal boundary" "Playback internals must not depend on application command/query services"
            }
        }

        if ($repoPath -eq "src/app/playback_controller.cpp" -or $repoPath -eq "src/app/playback_controller.h") {
            if ($line -match '\bstd::thread\b|\bstd::mutex\b|pending_enrichment|enrichment_threads|requestEnrichment|applyPendingEnrichments|PlaybackSessionClock|PlaybackBackendController|PlaybackTransitionCoordinator|PlaybackCompletionPolicy|AudioPipeline|AudioPlaybackState|visualizationFrame\(') {
                Add-Violation $violations $repoPath $lineNumber "playback core ownership" "PlaybackController must own playback commands and queue semantics only; enrichment, clock, backend, transition, completion policy, and audio pipeline responsibilities belong to dedicated integrated-core components"
            }
        }

        if ($repoPath -eq "src/app/playback_runtime_coordinator.cpp" -or $repoPath -eq "src/app/playback_runtime_coordinator.h") {
            if ($line -match 'PlaybackBackendController|PlaybackVisualizationSource') {
                Add-Violation $violations $repoPath $lineNumber "audio pipeline facade" "PlaybackRuntimeCoordinator must use the AudioPipelineController facade instead of backend/visualization adapters directly"
            }
        }

        if ($repoPath -eq "src/platform/host/runtime_enrichment_clients.cpp") {
            if ($line -match '\b(AcoustIdIdentityClient|MusicBrainzMetadataClient|CoverArtArchiveClient|LrclibClient|LyricsOvhClient|FfprobeMetadataReader)::') {
                Add-Violation $violations $repoPath $lineNumber "enrichment protocol client" "runtime_enrichment_clients.cpp must stay shared helper/orchestration code; concrete protocol clients live in dedicated files"
            }
        }

        if ($repoPath -eq "src/platform/host/metadata_provider.cpp") {
            if ($line -match '\b(MusicBrainzMetadataClient|AcoustIdIdentityClient)::|\bMusicBrainzMetadataClient\s+\w+') {
                Add-Violation $violations $repoPath $lineNumber "metadata enrichment orchestration" "metadata_provider.cpp must delegate online lookup ordering to runtime_metadata_enrichment_orchestrator"
            }
        }

        if ($repoPath -eq "src/platform/host/runtime_services_factory.cpp") {
            if ($line -match '\bcreateHost(MetadataProvider|TrackIdentityProvider|ArtworkProvider|LyricsProvider|TagWriter|AudioPlaybackBackend|ConnectivityProvider|RemoteSourceProvider|RemoteCatalogProvider|RemoteStreamResolver)\s*\(') {
                Add-Violation $violations $repoPath $lineNumber "runtime service provider" "runtime_services_factory.cpp must compose host service providers and must not directly construct concrete runtime capabilities"
            }
        }

        if ($repoPath -eq "src/platform/host/lyrics_provider.cpp") {
            if ($line -match 'class\s+LyricsCache|class\s+LyricsWritebackPolicy') {
                Add-Violation $violations $repoPath $lineNumber "lyrics pipeline component" "LyricsProvider must compose lyrics pipeline components instead of owning cache/writeback policy internals"
            }
        }

        if ($repoPath -eq "src/app/app_renderer.cpp") {
            if ($line -match '\b(toUiSpectrumFrame|toUiLyricsContent|toUiPlaybackStatus|toUiIndexState|playbackSummary|emptyLabelForPage|formatStorage)\b') {
                Add-Violation $violations $repoPath $lineNumber "projection builder ownership" "AppRenderer must dispatch pages only; app-state to UI projection belongs to AppProjectionBuilder"
            }
        }

        if ($repoPath -eq "src/app/library_controller.cpp") {
            if ($line -match 'case\s+AppPage::|case\s+\d+\s*:|LibraryDatabase|LibraryStore|LibraryIndexer|LibraryMutationService|LibraryScanScheduler|RemoteSource|RemoteSourceRegistry') {
                Add-Violation $violations $repoPath $lineNumber "library fact ownership" "LibraryController must not own list-open branching, durable library storage/indexing, or remote provider access"
            }
        }

        if ($repoPath -eq "src/app/library_controller.h") {
            if ($line -match '^\s*(LibraryIndexState|LibraryModel)\s+\w+_\{') {
                Add-Violation $violations $repoPath $lineNumber "library repository ownership" "LibraryController must keep library fact storage behind LibraryRepository"
            }
        }

        if ($repoPath.StartsWith("src/remote/", [System.StringComparison]::Ordinal)) {
            if ($line -match '\bPlaybackController\b|\bPlaybackSession\b|ui::|pages::') {
                Add-Violation $violations $repoPath $lineNumber "remote source boundary" "Remote providers must produce catalog/stream facts and must not control playback or UI"
            }
        }

        if ($repoPath.StartsWith("src/metadata/", [System.StringComparison]::Ordinal)) {
            if ($line -match 'ui::|pages::|PlaybackController') {
                Add-Violation $violations $repoPath $lineNumber "metadata boundary" "Metadata/enrichment code must not depend on UI pages or playback controllers"
            }
        }

        if ($repoPath.StartsWith("src/desktop/", [System.StringComparison]::Ordinal)) {
            if ($line -match 'ui::|pages::|AudioPlaybackBackend|RemoteSourceProvider|RemoteCatalogProvider|RemoteStreamResolver') {
                Add-Violation $violations $repoPath $lineNumber "desktop projection boundary" "Desktop integration must consume app commands/projections and must not depend on UI pages, playback backends, or remote providers"
            }
        }

        if ($repoPath -eq "src/ui/pages/lyrics_page.cpp") {
            if ($line -match '^\s*(void|int|std::optional|std::vector<|struct)\s+(renderFoamSide|renderSideFoamSpectrum|parseLyricTimestamp|lyricDisplayLines|activeLyricIndex|LyricDisplayLine)\b|struct\s+LyricDisplayLine') {
                Add-Violation $violations $repoPath $lineNumber "lyrics page algorithm ownership" "LyricsPage must compose layout/effect widgets; lyric parsing and spectrum algorithms belong to ui/widgets or ui/effects"
            }
        }

        if ($line -match '\bservices(_snapshot|_)?\.(metadata_provider|track_identity_provider|artwork_provider|audio_playback_backend|lyrics_provider|tag_writer|connectivity_provider|remote_source_provider|remote_catalog_provider|remote_stream_resolver)\b') {
            Add-Violation $violations $repoPath $lineNumber "runtime service grouping" "Runtime services must be accessed through capability groups: connectivity, metadata, playback, or remote"
        }

        if ($line -notmatch $includeRegex) {
            continue
        }

        $include = $Matches[1]

        if ($repoPath.StartsWith("src/core/", [System.StringComparison]::Ordinal)) {
            if (Test-AnyPrefix $include @("app/", "platform/", "audio/", "metadata/", "library/", "playback/", "remote/", "desktop/", "security/", "ui/")) {
                Add-Violation $violations $repoPath $lineNumber $include "core must not depend on higher layers"
            }
        }

        if ($repoPath.StartsWith("src/ui/", [System.StringComparison]::Ordinal)) {
            if (Test-AnyPrefix $include @("app/", "platform/", "audio/", "metadata/", "library/", "playback/", "remote/", "desktop/", "security/")) {
                Add-Violation $violations $repoPath $lineNumber $include "UI code must use UI projection types and must not include app, platform, or backend layers"
            }
        }

        if ($repoPath.StartsWith("src/app/", [System.StringComparison]::Ordinal) -and -not $repoPath.Contains("_runner.")) {
            if (Test-AnyPrefix $include @("platform/host/", "platform/device/", "platform/x11/", "platform/wayland/")) {
                Add-Violation $violations $repoPath $lineNumber $include "shared app code must not include concrete platform adapters"
            }
        }

        if ($repoPath.StartsWith("src/application/", [System.StringComparison]::Ordinal)) {
            if (Test-AnyPrefix $include @("platform/", "targets/", "ui/pages/")) {
                Add-Violation $violations $repoPath $lineNumber $include "application command/query services must not include concrete platform adapters, targets, or UI pages"
            }
        }

        if ($repoPath.StartsWith("src/runtime/", [System.StringComparison]::Ordinal)) {
            if (Test-AnyPrefix $include @("platform/", "targets/", "ui/", "app/app_runtime_context.h", "cli/")) {
                Add-Violation $violations $repoPath $lineNumber $include "runtime command/session code must not include platform transports, targets, UI, AppRuntimeContext, or CLI adapters"
            }
        }

        if ($repoPath.StartsWith("src/cli/", [System.StringComparison]::Ordinal)) {
            if (Test-AnyPrefix $include @("platform/", "targets/", "ui/", "app/app_runtime_context.h", "playback/playback_controller.h")) {
                Add-Violation $violations $repoPath $lineNumber $include "CLI parsing/formatting must not depend on concrete platform adapters, targets, GUI runtime, UI, or playback internals"
            }
            $allowedRuntimeCliIncludes = @(
                "runtime/runtime_command.h",
                "runtime/runtime_command_client.h",
                "runtime/runtime_result.h",
                "runtime/runtime_snapshot.h",
                "runtime/unix_socket_runtime_transport.h"
            )
            if ((Test-AnyPrefix $include @("runtime/")) -and ($include -notin $allowedRuntimeCliIncludes)) {
                Add-Violation $violations $repoPath $lineNumber $include "Runtime CLI may include only the runtime command/client/result/snapshot/transport client contract, not runtime host internals"
            }
        }

        if ($repoPath.StartsWith("src/tui/", [System.StringComparison]::Ordinal)) {
            if (Test-AnyPrefix $include @("app/app_runtime_context.h", "audio/", "metadata/", "remote/", "security/", "platform/host/", "platform/device/", "platform/x11/", "platform/wayland/", "ui/pages/", "playback/playback_backend_controller.h")) {
                Add-Violation $violations $repoPath $lineNumber $include "TUI must stay a terminal runtime projection and must not include GUI runtime, audio/metadata/remote/security internals, host/device/X11/Wayland adapters, or UI page implementations"
            }
        }

        if ($repoPath -eq "src/app/lofibox_app.cpp") {
            if (Test-AnyPrefix $include @("app/input_actions.h", "core/bitmap_font.h", "core/color.h", "ui/pages/", "ui/ui_primitives.h", "ui/ui_theme.h")) {
                Add-Violation $violations $repoPath $lineNumber $include "LoFiBoxApp composition must use dedicated page-model/routing/rendering helpers instead of owning input mapping, page rendering, or UI constants"
            }
            if (Test-AnyPrefix $include @("app/app_command_executor.h", "app/app_input_router.h", "app/app_lifecycle.h", "app/app_page_model.h", "app/app_renderer.h", "app/app_state.h", "app/library_controller.h", "app/navigation_state.h", "app/playback_controller.h")) {
                Add-Violation $violations $repoPath $lineNumber $include "LoFiBoxApp must stay a composition root; GUI routing/rendering/lifecycle belongs to AppRuntimeContext, app controllers belong to AppServiceHost, and live runtime belongs to RuntimeHost"
            }
        }

        if ($repoPath -eq "src/app/app_runtime_context.h") {
            if (Test-AnyPrefix $include @("app/app_state.h", "app/library_controller.h", "app/navigation_state.h", "app/playback_controller.h")) {
                Add-Violation $violations $repoPath $lineNumber $include "AppRuntimeContext must depend on AppRuntimeState, AppServiceHost, and RuntimeCommandClient instead of directly including raw state/controller owners"
            }
        }

        if ($repoPath.StartsWith("src/platform/host/", [System.StringComparison]::Ordinal)) {
            if (Test-AnyPrefix $include @("ui/pages/", "app/lofibox_app.h")) {
                Add-Violation $violations $repoPath $lineNumber $include "host adapters must not depend on concrete app or pages"
            }
        }

        if ($repoPath.StartsWith("src/platform/device/", [System.StringComparison]::Ordinal) -or $repoPath.StartsWith("src/platform/x11/", [System.StringComparison]::Ordinal) -or $repoPath.StartsWith("src/platform/wayland/", [System.StringComparison]::Ordinal)) {
            if (Test-AnyPrefix $include @("ui/pages/", "app/lofibox_app.h", "platform/host/")) {
                Add-Violation $violations $repoPath $lineNumber $include "presentation adapters must not depend on host runtime or concrete app/pages"
            }
        }

        if ($repoPath.StartsWith("src/targets/", [System.StringComparison]::Ordinal)) {
            if (Test-AnyPrefix $include @("ui/pages/")) {
                Add-Violation $violations $repoPath $lineNumber $include "targets must stay thin and must not include page implementations"
            }
        }
    }
}

if ($violations.Count -gt 0) {
    if (-not $Quiet) {
        Write-Error ("Architecture boundary violations:`n" + ($violations -join "`n"))
    }
    exit 1
}

if (-not $Quiet) {
    Write-Output "Architecture boundary check passed."
}
