param(
    [ValidateSet("Debug", "Release")]
    [string]$Configuration = "Release"
)

$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "local-paths.ps1")
$surge = $surgeVst3
$invalidPlugin = Join-Path $projectRoot "tests\not-a-plugin.vst3"
$artifacts = Join-Path $projectRoot "artifacts"

function Find-BuiltBinary([string]$Name) {
    $binary = Get-ChildItem -LiteralPath $buildDir -Recurse -Filter $Name |
        Where-Object { $_.FullName -match "\\$Configuration\\" } |
        Select-Object -First 1

    if ($null -eq $binary) {
        throw "$Name was not found. Run build.ps1 first."
    }

    return $binary.FullName
}

if (-not (Test-Path -LiteralPath $surge)) {
    throw "The isolated Surge XT VST3 was not found at $surge"
}

if (-not (Test-Path -LiteralPath $invalidPlugin)) {
    throw "The invalid VST3 fixture was not found at $invalidPlugin"
}

$scanner = Find-BuiltBinary "ResonancePluginScanner.exe"
$inventoryController = Find-BuiltBinary "ResonancePluginInventory.exe"
$hangFixture = Find-BuiltBinary "ScannerHangFixture.exe"

New-Item -ItemType Directory -Path $artifacts -Force | Out-Null

$timeoutInventory = Join-Path $artifacts "scanner-timeout-inventory.json"
$timeoutQuarantine = Join-Path $artifacts "scanner-timeout-quarantine.json"
$invalidInventory = Join-Path $artifacts "scanner-invalid-inventory.json"
$invalidQuarantine = Join-Path $artifacts "scanner-invalid-quarantine.json"
$inventory = Join-Path $artifacts "plugin-inventory.json"
$quarantine = Join-Path $artifacts "plugin-quarantine.json"

Remove-Item -LiteralPath $timeoutInventory,$timeoutQuarantine,$invalidInventory,$invalidQuarantine -Force -ErrorAction SilentlyContinue

$timeoutClock = [System.Diagnostics.Stopwatch]::StartNew()
& $inventoryController --scanner $hangFixture --plugin $surge --inventory $timeoutInventory --quarantine $timeoutQuarantine --timeout-ms 250
$timeoutExit = $LASTEXITCODE
$timeoutClock.Stop()

if ($timeoutExit -ne 21) {
    throw "The hang fixture returned $timeoutExit instead of the timeout code 21"
}

if ($timeoutClock.ElapsedMilliseconds -gt 5000) {
    throw "Timeout isolation took $($timeoutClock.ElapsedMilliseconds) ms; the helper was not stopped promptly"
}

if (Get-Process -Name "ScannerHangFixture" -ErrorAction SilentlyContinue) {
    throw "The timed-out scanner fixture is still running"
}

$timeoutRecord = Get-Content -LiteralPath $timeoutQuarantine -Raw | ConvertFrom-Json
$timeoutEntries = @($timeoutRecord.entries)

if ($timeoutEntries.Count -ne 1 -or $timeoutEntries[0].failureKind -ne "timeout") {
    throw "The timeout was not persisted as one quarantine entry"
}

if ($timeoutEntries[0].bundleFingerprintSha256 -notmatch "^[0-9a-f]{64}$") {
    throw "The quarantine entry is missing its exact bundle fingerprint"
}

& $inventoryController --scanner $scanner --plugin $invalidPlugin --inventory $invalidInventory --quarantine $invalidQuarantine --timeout-ms 5000
$invalidExit = $LASTEXITCODE

if ($invalidExit -ne 22) {
    throw "The invalid VST3 fixture returned $invalidExit instead of scanner-exit code 22"
}

$invalidRecord = Get-Content -LiteralPath $invalidQuarantine -Raw | ConvertFrom-Json
$invalidEntries = @($invalidRecord.entries)

if ($invalidEntries.Count -ne 1 -or $invalidEntries[0].failureKind -ne "scanner-exit") {
    throw "The invalid VST3 was not persisted as one scanner-exit quarantine entry"
}

if ($invalidEntries[0].detail -notmatch "No VST3 types were discovered") {
    throw "The structured scanner error was not preserved in quarantine"
}

& $inventoryController --scanner $scanner --plugin $surge --inventory $inventory --quarantine $quarantine --timeout-ms 20000
if ($LASTEXITCODE -ne 0) {
    throw "The real Surge XT scan failed with exit code $LASTEXITCODE"
}

$inventoryRecord = Get-Content -LiteralPath $inventory -Raw | ConvertFrom-Json
$inventoryEntries = @($inventoryRecord.plugins)
$quarantineRecord = Get-Content -LiteralPath $quarantine -Raw | ConvertFrom-Json
$quarantineEntries = @($quarantineRecord.entries)
$surgeInventoryEntries = @($inventoryEntries | Where-Object { $_.bundlePath -eq $surge })
$surgeQuarantineEntries = @($quarantineEntries | Where-Object { $_.bundlePath -eq $surge })

if ($surgeInventoryEntries.Count -ne 1) {
    throw "Expected exactly one accepted Surge XT record, found $($surgeInventoryEntries.Count)"
}

if ($inventoryEntries.Count -ne 1) {
    throw "Relocating Surge left stale inventory entries: $($inventoryEntries.Count) total records"
}

if ($surgeInventoryEntries[0].uniqueId -ne 420368317 -or
    $surgeInventoryEntries[0].identifier -notmatch '^VST3-Surge XT-[0-9a-fA-F]{8}-190e4fbd$') {
    throw "The accepted inventory record is not the expected Surge XT VST3"
}

if ($surgeInventoryEntries[0].bundleFingerprintSha256 -ne $timeoutEntries[0].bundleFingerprintSha256) {
    throw "The accepted and quarantined test paths did not fingerprint the same VST3 bundle"
}

if ($surgeQuarantineEntries.Count -ne 0) {
    throw "A successful rescan did not clear Surge XT from the production quarantine"
}

[pscustomobject]@{
    TimeoutExitCode = $timeoutExit
    TimeoutElapsedMs = $timeoutClock.ElapsedMilliseconds
    TimeoutFailureKind = $timeoutEntries[0].failureKind
    InvalidExitCode = $invalidExit
    InvalidFailureKind = $invalidEntries[0].failureKind
    BundleFingerprintSha256 = $surgeInventoryEntries[0].bundleFingerprintSha256
    AcceptedIdentifier = $surgeInventoryEntries[0].identifier
    AcceptedVersion = $surgeInventoryEntries[0].version
    AcceptedParameterCount = $surgeInventoryEntries[0].parameterCount
    InventoryEntries = $inventoryEntries.Count
    SurgeQuarantineEntries = $surgeQuarantineEntries.Count
    InventoryPath = $inventory
    QuarantinePath = $quarantine
}
