param(
    [ValidateSet("Debug", "Release")]
    [string]$Configuration = "Release"
)

$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "local-paths.ps1")
$surge = $surgeVst3
$artifacts = Join-Path $projectRoot "artifacts"
$wav = Join-Path $artifacts "surge-xt-smoke.wav"
$report = Join-Path $artifacts "surge-xt-smoke-report.json"

$probe = Get-ChildItem -LiteralPath $buildDir -Recurse -Filter "ResonanceHostProbe.exe" |
    Where-Object { $_.FullName -match "\\$Configuration\\" } |
    Select-Object -First 1

if ($null -eq $probe) {
    throw "Build the host probe before running the Surge XT test"
}

if (-not (Test-Path -LiteralPath $surge)) {
    throw "The isolated Surge XT VST3 was not found at $surge"
}

New-Item -ItemType Directory -Path $artifacts -Force | Out-Null
& $probe.FullName --plugin $surge --wav $wav --report $report
if ($LASTEXITCODE -ne 0) { throw "Surge XT host probe failed with exit code $LASTEXITCODE" }

Get-Item -LiteralPath $wav, $report | Select-Object FullName,Length,LastWriteTime
