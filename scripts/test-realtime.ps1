param(
    [ValidateSet("Debug", "Release")]
    [string]$Configuration = "Release"
)

$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "local-paths.ps1")
$artifacts = Join-Path $projectRoot "artifacts"
$inventory = Join-Path $artifacts "plugin-inventory.json"
$quarantine = Join-Path $artifacts "plugin-quarantine.json"
$engineReport = Join-Path $artifacts "realtime-engine-test-report.json"
$projectReport = Join-Path $artifacts "song-project-test-report.json"
$selfTestReport = Join-Path $artifacts "realtime-self-test.json"
$m6RuntimeReport = Join-Path $artifacts "m6-runtime-test-report.json"
$m6AuthoringReport = Join-Path $artifacts "m6-authoring-test-report.json"
$m5WorkflowReport = Join-Path $artifacts "m5-workflow-test-report.json"
$commandLoadReport = Join-Path $artifacts "command-load-test-report.json"
$songProjectArtifact = Join-Path $artifacts "realtime-song-project.resonance.json"
$m6AuthoringProject = Join-Path $artifacts "m6-two-track-authoring.resonance.json"
$uiSnapshot = Join-Path $artifacts "realtime-ui-snapshot.png"
$editCommandFixture = Join-Path $projectRoot "tests\fixtures\edit-command-note-patch-v1.json"
$legacyProjectFixture = Join-Path $projectRoot "tests\fixtures\song-project-v1-migration.resonance.json"
$previousProjectFixture = Join-Path $projectRoot "tests\fixtures\song-project-v2-migration.resonance.json"
$m4AcceptedFixture = Join-Path $artifacts "m4-accepted-candidate-b.resonance.json"
$expectedM4AcceptedFixtureSha256 = "B0265238EF823D660B198C6730066CAACE09E001EAE3B3D3410521938FE74172"

function Find-BuiltBinary([string]$Name) {
    $binary = Get-ChildItem -LiteralPath $buildDir -Recurse -Filter $Name |
        Where-Object { $_.FullName -match "\\$Configuration\\" } |
        Select-Object -First 1

    if ($null -eq $binary) {
        throw "$Name was not found. Run build.ps1 first."
    }

    return $binary.FullName
}

$engineTests = Find-BuiltBinary "RealtimeEngineTests.exe"
$projectTests = Find-BuiltBinary "SongProjectTests.exe"
$editor = Join-Path $projectRoot "bin\ResonanceMusicEditor.exe"

if (-not (Test-Path -LiteralPath $editor)) {
    throw "The packaged editor was not found. Run build.ps1 first."
}

New-Item -ItemType Directory -Path $artifacts -Force | Out-Null
Remove-Item -LiteralPath $engineReport,$projectReport,$selfTestReport,$m6RuntimeReport,$m6AuthoringReport,$m5WorkflowReport,$commandLoadReport,$songProjectArtifact,$m6AuthoringProject,$uiSnapshot -Force -ErrorAction SilentlyContinue

if (-not (Test-Path -LiteralPath $m4AcceptedFixture) -or
    (Get-FileHash -Algorithm SHA256 -LiteralPath $m4AcceptedFixture).Hash -ne
        $expectedM4AcceptedFixtureSha256) {
    throw "The exact accepted M4 candidate-B fixture is missing or changed"
}

& $engineTests --report $engineReport
if ($LASTEXITCODE -ne 0) {
    throw "Realtime scheduler tests failed with exit code $LASTEXITCODE"
}

$engineResult = Get-Content -LiteralPath $engineReport -Raw | ConvertFrom-Json
if (-not $engineResult.passed -or $engineResult.assertions -lt 124 -or
    $engineResult.maxMixerTracks -ne 8 -or -not $engineResult.mixerContractPassed -or
    -not $engineResult.twoTrackRuntimePassed -or
    $engineResult.twoTrackAverageCallbackLoad -ge 0.25) {
    throw "Realtime scheduler report did not pass its assertion gate"
}

& $projectTests --report $projectReport --edit-command-fixture $editCommandFixture `
    --legacy-project-fixture $legacyProjectFixture `
    --previous-project-fixture $previousProjectFixture
if ($LASTEXITCODE -ne 0) {
    throw "Song project tests failed with exit code $LASTEXITCODE"
}

$projectResult = Get-Content -LiteralPath $projectReport -Raw | ConvertFrom-Json
if (-not $projectResult.passed -or $projectResult.assertions -lt 209) {
    throw "Song project report did not pass its assertion gate"
}

if ($projectResult.projectSchemaVersion -ne 3 -or
    $projectResult.legacySchemaVersion -ne 1 -or
    $projectResult.previousSchemaVersion -ne 2 -or
    -not $projectResult.legacyMigrationPassed -or
    -not $projectResult.previousMigrationPassed -or
    $projectResult.maxProjectTracks -ne 2 -or
    -not $projectResult.twoTrackTopologyPassed -or
    $projectResult.stableTrackId -ne "track-migrated" -or
    $projectResult.stableClipId -ne "clip-migrated") {
    throw "The version-1/version-2 to version-3 project and topology gate did not pass"
}

if ($projectResult.seededVelocitySeed -ne 18421 -or
    $projectResult.seededVelocityMaximumDelta -ne 8 -or
    $projectResult.seededVelocityCommandSha256 -notmatch '^[0-9a-f]{64}$' -or
    $projectResult.seededVelocityCandidateSha256 -notmatch '^[0-9a-f]{64}$') {
    throw "The seeded velocity resolver did not produce its deterministic evidence"
}

$selfTest = Start-Process -FilePath $editor -ArgumentList "--self-test" -WorkingDirectory $projectRoot -Wait -PassThru -WindowStyle Hidden

if ($selfTest.ExitCode -ne 0) {
    if (Test-Path -LiteralPath $selfTestReport) {
        Get-Content -LiteralPath $selfTestReport
    }
    throw "Realtime editor self-test failed with exit code $($selfTest.ExitCode)"
}

$result = Get-Content -LiteralPath $selfTestReport -Raw | ConvertFrom-Json
if (-not $result.passed) {
    throw "Realtime editor self-test did not return passed: true"
}

if ($result.device.type -notmatch "Windows Audio") {
    throw "The self-test did not open the JUCE Windows Audio/WASAPI backend"
}

if ($result.audioEmitted -or -not $result.noRescanPerformed) {
    throw "The silent self-test violated its no-audio or no-rescan contract"
}

if ($result.plugin.identifier -notmatch '^VST3-Surge XT-[0-9a-fA-F]{8}-190e4fbd$') {
    throw "The self-test did not load the accepted Surge XT inventory record"
}

if ($result.plugin.parameterCount -ne 2855 -or -not $result.parameterCountMatchesInventory) {
    throw "The live Surge parameter count did not match the accepted inventory"
}

if (-not $result.songProject.savedPayloadExact -or -not $result.songProject.pluginRestoreExact) {
    throw "The real Surge song-project state did not round-trip exactly"
}

if (-not $result.songProject.soundNameRoundTrip -or $result.songProject.soundName -ne "Self-test Surge state") {
    throw "The host-owned sound name did not round-trip with the real Surge state"
}

if ($result.songProject.schemaVersion -ne 3 -or
    $result.songProject.trackId -ne "track-1" -or
    $result.songProject.clipId -ne "loop-1" -or
    $result.songProject.mixerGainDb -ne 0 -or
    $result.songProject.mixerPan -ne 0 -or
    $result.songProject.mixerMuted -or $result.songProject.mixerSolo -or
    $result.songProject.midiInputChannel -ne 0 -or
    $result.songProject.midiOutputChannel -ne 1) {
    throw "The packaged editor did not preserve the schema-v3 identity, mixer, and MIDI defaults"
}

if ($result.songProject.noteCount -ne 9 -or $result.songProject.loopLengthBeats -ne 16 -or
    $result.songProject.fixtureNoteId -ne "note-self-test-1") {
    throw "The live song-project round trip lost editable note or loop data"
}

if (Get-Process -Name "ResonanceMusicEditor" -ErrorAction SilentlyContinue) {
    throw "The hidden self-test left an editor process running"
}

$m6RuntimeTest = Start-Process -FilePath $editor -ArgumentList "--m6-runtime-test","--report",$m6RuntimeReport -WorkingDirectory $projectRoot -Wait -PassThru -WindowStyle Hidden

if ($m6RuntimeTest.ExitCode -ne 0) {
    if (Test-Path -LiteralPath $m6RuntimeReport) {
        Get-Content -LiteralPath $m6RuntimeReport
    }
    throw "M6 two-track runtime test failed with exit code $($m6RuntimeTest.ExitCode)"
}

$m6Result = Get-Content -LiteralPath $m6RuntimeReport -Raw | ConvertFrom-Json
if (-not $m6Result.passed -or $m6Result.runtimeCapacity -ne 8 -or
    $m6Result.audioEmitted -or -not $m6Result.noRescanPerformed) {
    throw "The M6 runtime violated its fixed-capacity, silent, or no-rescan contract"
}

if ($m6Result.device.type -notmatch "Windows Audio" -or
    -not $m6Result.plugin.distinctInstances -or
    $m6Result.plugin.firstParameterCount -ne 2855 -or
    $m6Result.plugin.secondParameterCount -ne 2855 -or
    $m6Result.plugin.alternateStateSha256 -ne
        "ccaf99d4dc86d0b272e6ff1cc3be8afd07349bbcfe5055d992a001bea74da308" -or
    $m6Result.plugin.normalisedAlternateStateSha256 -ne
        "91ed214e64b35e95cf20ca773ccf57f650bbeecb547d1aa5f0ba8a2f2f5c36a3" -or
    $m6Result.plugin.alternateStatePreservedExact -or
    -not $m6Result.plugin.alternateStateApplied -or
    -not $m6Result.plugin.independentStateMutation -or
    -not $m6Result.plugin.completeStateRoundTrip) {
    throw "The M6 gate did not prove two independent accepted Surge instances and exact state"
}

if ($m6Result.runtime.installedInstances -ne 2 -or
    -not $m6Result.runtime.bothTracksProcessed -or
    $m6Result.runtime.maximumOutputPeak -le 0 -or
    $m6Result.runtime.maximumTrackOnePeak -le 0 -or
    $m6Result.runtime.maximumTrackTwoPeak -le 0 -or
    $m6Result.runtime.averageCallbackLoad -ge 1 -or
    $m6Result.runtime.invalidSamples -ne 0 -or
    $m6Result.runtime.clippedSamples -ne 0 -or
    $m6Result.runtime.processorExceptions -ne 0 -or
    -not $m6Result.runtime.missingPluginPreserved -or
    -not $m6Result.runtime.shutdownComplete) {
    throw "The M6 two-track render, safety, missing-slot, or shutdown gate failed"
}

if (Get-Process -Name "ResonanceMusicEditor" -ErrorAction SilentlyContinue) {
    throw "The M6 runtime test left an editor process running"
}

$m6AuthoringTest = Start-Process -FilePath $editor -ArgumentList `
    "--m6-authoring-test","--project",$m6AuthoringProject,"--report",$m6AuthoringReport `
    -WorkingDirectory $projectRoot -Wait -PassThru -WindowStyle Hidden

if ($m6AuthoringTest.ExitCode -ne 0) {
    if (Test-Path -LiteralPath $m6AuthoringReport) {
        Get-Content -LiteralPath $m6AuthoringReport
    }
    throw "M6 two-track authoring test failed with exit code $($m6AuthoringTest.ExitCode)"
}

$m6AuthoringResult = Get-Content -LiteralPath $m6AuthoringReport -Raw | ConvertFrom-Json
if (-not $m6AuthoringResult.passed -or $m6AuthoringResult.audioEmitted -or
    $m6AuthoringResult.preloadedPluginCount -ne 2 -or
    -not $m6AuthoringResult.distinctRuntimeInstances -or
    -not $m6AuthoringResult.addTrackSucceeded -or
    -not $m6AuthoringResult.stableDistinctIds -or
    -not $m6AuthoringResult.duplicatedStateExact -or
    -not $m6AuthoringResult.runtimeStateAlignedAfterAdd -or
    -not $m6AuthoringResult.independentNotes -or
    -not $m6AuthoringResult.independentMixerSettings) {
    throw "The M6 visible add-track, independent state, note, or mixer authoring gate failed"
}

if (-not $m6AuthoringResult.reorderSucceeded -or
    -not $m6AuthoringResult.runtimeStateAlignedAfterReorder -or
    -not $m6AuthoringResult.undoReorderRestored -or
    -not $m6AuthoringResult.removeSucceeded -or
    -not $m6AuthoringResult.undoRemoveRestored) {
    throw "The M6 track reorder/remove runtime remap or Undo gate failed"
}

if (-not $m6AuthoringResult.saveSucceeded -or
    $m6AuthoringResult.reopenedSchemaVersion -ne 3 -or
    $m6AuthoringResult.reopenedTrackCount -ne 2 -or
    -not $m6AuthoringResult.reopenedOrderPreserved -or
    -not $m6AuthoringResult.reopenedMixerPreserved -or
    -not $m6AuthoringResult.reopenedIndependentNotes -or
    $m6AuthoringResult.invalidSampleCount -ne 0 -or
    $m6AuthoringResult.processorExceptionCount -ne 0) {
    throw "The M6 two-track Save/Open or runtime-safety authoring gate failed"
}

if (Get-Process -Name "ResonanceMusicEditor" -ErrorAction SilentlyContinue) {
    throw "The M6 authoring test left an editor process running"
}

$m5WorkflowTest = Start-Process -FilePath $editor -ArgumentList "--m5-workflow-test","--report",$m5WorkflowReport -WorkingDirectory $projectRoot -Wait -PassThru -WindowStyle Hidden

if ($m5WorkflowTest.ExitCode -ne 0) {
    if (Test-Path -LiteralPath $m5WorkflowReport) {
        Get-Content -LiteralPath $m5WorkflowReport
    }
    throw "M5 proposal workflow test failed with exit code $($m5WorkflowTest.ExitCode)"
}

$m5Result = Get-Content -LiteralPath $m5WorkflowReport -Raw | ConvertFrom-Json
if (-not $m5Result.passed -or -not $m5Result.previewCreated -or
    -not $m5Result.activeUnchangedDuringPreview -or -not $m5Result.projectCleanDuringPreview -or
    -not $m5Result.savePreservedAcceptedA) {
    throw "The M5 proposal preview mutated or failed to preserve the active project"
}

if (-not $m5Result.soundLaneInterlocked) {
    throw "The simultaneous sound and note candidate lanes were not interlocked"
}

if (-not $m5Result.candidateAuditionSelected -or -not $m5Result.projectAuditionSelected -or
    -not $m5Result.rejectedWithoutMutation) {
    throw "The M5 A/B audition or Reject lifecycle failed"
}

if (-not $m5Result.applied -or -not $m5Result.applyProducedOneUndo -or
    -not $m5Result.undoRestoredBefore -or -not $m5Result.redoRestoredCandidate) {
    throw "The M5 Apply and one-Undo/Redo contract failed"
}

if (-not $m5Result.stalePreviewInvalidated -or -not $m5Result.finalRestored -or
    -not $m5Result.closeAcceptedWithoutWarning) {
    throw "The M5 stale-preview or lifecycle cleanup contract failed"
}

$commandLoadTest = Start-Process -FilePath $editor -ArgumentList "--command-load-test","--report",$commandLoadReport -WorkingDirectory $projectRoot -Wait -PassThru -WindowStyle Hidden

if ($commandLoadTest.ExitCode -ne 0) {
    if (Test-Path -LiteralPath $commandLoadReport) {
        Get-Content -LiteralPath $commandLoadReport
    }
    throw "External command-load test failed with exit code $($commandLoadTest.ExitCode)"
}

$commandLoadResult = Get-Content -LiteralPath $commandLoadReport -Raw | ConvertFrom-Json
if (-not $commandLoadResult.staleHashRefused -or -not $commandLoadResult.wrongTrackRefused -or
    -not $commandLoadResult.wrongClipRefused -or -not $commandLoadResult.malformedRefused -or
    -not $commandLoadResult.oversizeRefused -or -not $commandLoadResult.missingFileRefused) {
    throw "An invalid external edit command was not refused"
}

if (-not $commandLoadResult.previewCreated -or -not $commandLoadResult.candidateCarriesEdit -or
    -not $commandLoadResult.activeUnchangedDuringPreview -or
    -not $commandLoadResult.soundLaneInterlocked) {
    throw "The external command preview failed or mutated the active project"
}

if (-not $commandLoadResult.appliedAsOneTransaction -or -not $commandLoadResult.undoneInOneStep -or
    -not $commandLoadResult.replayAfterApplyRefused) {
    throw "The external command Apply/Undo contract failed"
}

if ($m5Result.seededVelocitySeed -ne 18421 -or
    $m5Result.seededVelocityMaximumDelta -ne 8 -or
    $m5Result.seededVelocityDiffCount -ne 8 -or
    -not $m5Result.seededVelocityPreviewCreated -or
    -not $m5Result.seededVelocityBounded -or
    -not $m5Result.seededVelocityRepeatMatched) {
    throw "The M5 seeded loop-dynamics preview was not deterministic and bounded"
}

if (-not $m5Result.seededVelocityAuditionPreservedA -or
    -not $m5Result.seededVelocityRejectedWithoutMutation -or
    -not $m5Result.seededVelocityApplied -or
    -not $m5Result.seededVelocityApplyProducedOneUndo -or
    -not $m5Result.seededVelocityUndoRestoredA) {
    throw "The M5 seeded loop-dynamics lifecycle failed"
}

if (-not $m5Result.parameterControlsAvailable -or
    $m5Result.parameterizedScope -ne "selectedNote" -or
    $m5Result.parameterizedSeed -ne 90210 -or
    $m5Result.parameterizedMaximumDelta -ne 3 -or
    $m5Result.parameterizedDiffCount -ne 1 -or
    -not $m5Result.parameterizedPreviewCreated -or
    -not $m5Result.parameterizedSummaryMatched -or
    -not $m5Result.parameterizedTargetMatched -or
    -not $m5Result.parameterizedBounded -or
    -not $m5Result.parameterizedCandidateDiffered) {
    throw "The M5 target, strength, and seed controls did not resolve the requested command"
}

if (-not $m5Result.parameterizedAuditionPreservedA -or
    -not $m5Result.parameterizedRejectedWithoutMutation -or
    -not $m5Result.parameterizedRepeatMatched -or
    -not $m5Result.parameterizedApplied -or
    -not $m5Result.parameterizedApplyProducedOneUndo -or
    -not $m5Result.parameterizedUndoRestoredA -or
    -not $m5Result.invalidDynamicsSettingsBlocked) {
    throw "The M5 parameterized dynamics lifecycle or invalid-settings guard failed"
}

if (Get-Process -Name "ResonanceMusicEditor" -ErrorAction SilentlyContinue) {
    throw "The M5 workflow test left an editor process running"
}

$snapshotTest = Start-Process -FilePath $editor -ArgumentList "--ui-snapshot" -WorkingDirectory $projectRoot -Wait -PassThru -WindowStyle Hidden

if ($snapshotTest.ExitCode -ne 0) {
    throw "Realtime editor UI snapshot failed with exit code $($snapshotTest.ExitCode)"
}

if (-not (Test-Path -LiteralPath $uiSnapshot)) {
    throw "Realtime editor UI snapshot was not created"
}

$snapshotBytes = (Get-Item -LiteralPath $uiSnapshot).Length
if ($snapshotBytes -lt 10000) {
    throw "Realtime editor UI snapshot was unexpectedly small: $snapshotBytes bytes"
}

if (Get-Process -Name "ResonanceMusicEditor" -ErrorAction SilentlyContinue) {
    throw "The UI snapshot test left an editor process running"
}

$idleWallClock = [System.Diagnostics.Stopwatch]::StartNew()
$idleTest = Start-Process -FilePath $editor -ArgumentList "--ui-idle-test" -WorkingDirectory $projectRoot -Wait -PassThru -WindowStyle Hidden
$idleWallClock.Stop()
$idleTest.Refresh()

if ($idleTest.ExitCode -ne 0) {
    throw "Realtime editor idle test failed with exit code $($idleTest.ExitCode)"
}

$idleCpuMilliseconds = $idleTest.TotalProcessorTime.TotalMilliseconds
if ($idleCpuMilliseconds -gt 3000) {
    throw "Realtime editor used $([math]::Round($idleCpuMilliseconds)) ms of CPU during the four-second UI idle gate"
}

if ($idleWallClock.ElapsedMilliseconds -lt 4000) {
    throw "Realtime editor idle gate exited before its four-second observation window"
}

if (Get-Process -Name "ResonanceMusicEditor" -ErrorAction SilentlyContinue) {
    throw "The UI idle test left an editor process running"
}

[pscustomobject]@{
    SchedulerAssertions = $engineResult.assertions
    MaxMixerTracks = $engineResult.maxMixerTracks
    MixerContractPassed = $engineResult.mixerContractPassed
    TwoTrackRuntimePassed = $engineResult.twoTrackRuntimePassed
    TwoTrackSyntheticAverageCallbackLoad = $engineResult.twoTrackAverageCallbackLoad
    ProjectAssertions = $projectResult.assertions
    ProjectSchemaVersion = $projectResult.projectSchemaVersion
    LegacySchemaVersion = $projectResult.legacySchemaVersion
    PreviousSchemaVersion = $projectResult.previousSchemaVersion
    LegacyMigrationPassed = $projectResult.legacyMigrationPassed
    LegacyMigrationSourceSha256 = $projectResult.legacySourceSha256
    PreviousMigrationPassed = $projectResult.previousMigrationPassed
    PreviousMigrationSourceSha256 = $projectResult.previousSourceSha256
    MigratedTrackId = $projectResult.stableTrackId
    MigratedClipId = $projectResult.stableClipId
    ProjectRoundTripBytes = $projectResult.roundTripBytes
    ProjectStateSha256 = $projectResult.stateSha256
    EditCommandVersion = $projectResult.editCommandVersion
    EditCommandFixture = $projectResult.editCommandFixture
    EditCommandCandidateSha256 = $projectResult.editCommandCandidateSha256
    SeededVelocityCommandSha256 = $projectResult.seededVelocityCommandSha256
    SeededVelocityUnitCandidateSha256 = $projectResult.seededVelocityCandidateSha256
    M5ProposalNote = $m5Result.selectedNoteId
    M5ProposalBeforeSha256 = $m5Result.beforeContentSha256
    M5ProposalCandidateSha256 = $m5Result.candidateContentSha256
    M5ProposalDiffs = $m5Result.diffCount
    M5StalePreviewInvalidated = $m5Result.stalePreviewInvalidated
    SeededVelocitySeed = $m5Result.seededVelocitySeed
    SeededVelocityMaximumDelta = $m5Result.seededVelocityMaximumDelta
    SeededVelocityCandidateSha256 = $m5Result.seededVelocityCandidateSha256
    SeededVelocityDiffs = $m5Result.seededVelocityDiffCount
    ParameterizedDynamicsScope = $m5Result.parameterizedScope
    ParameterizedDynamicsSeed = $m5Result.parameterizedSeed
    ParameterizedDynamicsMaximumDelta = $m5Result.parameterizedMaximumDelta
    ParameterizedDynamicsCandidateSha256 = $m5Result.parameterizedCandidateSha256
    ParameterizedDynamicsDiffs = $m5Result.parameterizedDiffCount
    InvalidDynamicsSettingsBlocked = $m5Result.invalidDynamicsSettingsBlocked
    CommandLoadCandidateSha256 = $commandLoadResult.candidateContentSha256
    CommandLoadDiffs = $commandLoadResult.noteDiffCount
    CommandLoadRefusalsPassed = ($commandLoadResult.staleHashRefused -and
        $commandLoadResult.wrongTrackRefused -and $commandLoadResult.wrongClipRefused -and
        $commandLoadResult.malformedRefused -and $commandLoadResult.oversizeRefused -and
        $commandLoadResult.missingFileRefused -and $commandLoadResult.replayAfterApplyRefused)
    LiveSurgeStateBytes = $result.songProject.stateBytes
    LiveSurgeStateSha256 = $result.songProject.stateSha256
    LiveSoundName = $result.songProject.soundName
    LiveSongProjectBytes = $result.songProject.fileBytes
    LoopNotes = $engineResult.noteCount
    DeviceType = $result.device.type
    DeviceName = $result.device.name
    SampleRate = $result.device.sampleRate
    BlockSize = $result.device.blockSize
    OutputLatencySamples = $result.device.outputLatencySamples
    PluginIdentifier = $result.plugin.identifier
    PluginVersion = $result.plugin.version
    PluginParameters = $result.plugin.parameterCount
    M6RuntimeInstances = $m6Result.runtime.installedInstances
    M6RuntimeBlocks = $m6Result.runtime.renderedBlocks
    M6RuntimeAverageCallbackLoad = $m6Result.runtime.averageCallbackLoad
    M6RuntimeMaximumCallbackLoad = $m6Result.runtime.maximumCallbackLoad
    M6RuntimeOutputPeak = $m6Result.runtime.maximumOutputPeak
    M6RuntimeTrackOnePeak = $m6Result.runtime.maximumTrackOnePeak
    M6RuntimeTrackTwoPeak = $m6Result.runtime.maximumTrackTwoPeak
    M6RuntimeStateRoundTrip = $m6Result.plugin.completeStateRoundTrip
    M6RuntimeMissingPluginPreserved = $m6Result.runtime.missingPluginPreserved
    M6AuthoringPreloadedInstances = $m6AuthoringResult.preloadedPluginCount
    M6AuthoringAddTrack = $m6AuthoringResult.addTrackSucceeded
    M6AuthoringIndependentNotes = $m6AuthoringResult.independentNotes
    M6AuthoringIndependentMixer = $m6AuthoringResult.independentMixerSettings
    M6AuthoringReorderRemapped = $m6AuthoringResult.runtimeStateAlignedAfterReorder
    M6AuthoringSaveOpen = $m6AuthoringResult.reopenedOrderPreserved
    M6AuthoringRemoveUndo = $m6AuthoringResult.undoRemoveRestored
    NoRescanPerformed = $result.noRescanPerformed
    AudioEmitted = $result.audioEmitted
    UiSnapshotBytes = $snapshotBytes
    UiIdleWallMilliseconds = $idleWallClock.ElapsedMilliseconds
    UiIdleCpuMilliseconds = [math]::Round($idleCpuMilliseconds, 1)
}
