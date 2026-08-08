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
$m5WorkflowReport = Join-Path $artifacts "m5-workflow-test-report.json"
$songProjectArtifact = Join-Path $artifacts "realtime-song-project.resonance.json"
$uiSnapshot = Join-Path $artifacts "realtime-ui-snapshot.png"
$editCommandFixture = Join-Path $projectRoot "tests\fixtures\edit-command-note-patch-v1.json"

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
Remove-Item -LiteralPath $engineReport,$projectReport,$selfTestReport,$m5WorkflowReport,$songProjectArtifact,$uiSnapshot -Force -ErrorAction SilentlyContinue

& $engineTests --report $engineReport
if ($LASTEXITCODE -ne 0) {
    throw "Realtime scheduler tests failed with exit code $LASTEXITCODE"
}

$engineResult = Get-Content -LiteralPath $engineReport -Raw | ConvertFrom-Json
if (-not $engineResult.passed -or $engineResult.assertions -lt 10) {
    throw "Realtime scheduler report did not pass its assertion gate"
}

& $projectTests --report $projectReport --edit-command-fixture $editCommandFixture
if ($LASTEXITCODE -ne 0) {
    throw "Song project tests failed with exit code $LASTEXITCODE"
}

$projectResult = Get-Content -LiteralPath $projectReport -Raw | ConvertFrom-Json
if (-not $projectResult.passed -or $projectResult.assertions -lt 50) {
    throw "Song project report did not pass its assertion gate"
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

if ($result.songProject.noteCount -ne 9 -or $result.songProject.loopLengthBeats -ne 16 -or
    $result.songProject.fixtureNoteId -ne "note-self-test-1") {
    throw "The live song-project round trip lost editable note or loop data"
}

if (Get-Process -Name "ResonanceMusicEditor" -ErrorAction SilentlyContinue) {
    throw "The hidden self-test left an editor process running"
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
    ProjectAssertions = $projectResult.assertions
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
    NoRescanPerformed = $result.noRescanPerformed
    AudioEmitted = $result.audioEmitted
    UiSnapshotBytes = $snapshotBytes
    UiIdleWallMilliseconds = $idleWallClock.ElapsedMilliseconds
    UiIdleCpuMilliseconds = [math]::Round($idleCpuMilliseconds, 1)
}
