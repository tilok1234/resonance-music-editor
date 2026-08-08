param(
    [ValidateSet("Debug", "Release")]
    [string]$Configuration = "Release"
)

$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "local-paths.ps1")
$cmake = "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"

if (-not (Test-Path -LiteralPath $cmake)) {
    throw "Visual Studio CMake was not found at $cmake"
}

if (-not (Test-Path -LiteralPath (Join-Path $juceDir "CMakeLists.txt"))) {
    throw "JUCE 9 was not found at $juceDir"
}

& $cmake -S $projectRoot -B $buildDir -G "Visual Studio 18 2026" -A x64 "-DJUCE_DIR=$juceDir"
if ($LASTEXITCODE -ne 0) { throw "CMake configuration failed with exit code $LASTEXITCODE" }

& $cmake --build $buildDir --config $Configuration --parallel
if ($LASTEXITCODE -ne 0) { throw "Build failed with exit code $LASTEXITCODE" }

$expectedBinaries = @(
    "ResonanceHostProbe.exe",
    "ResonancePluginScanner.exe",
    "ResonancePluginInventory.exe",
    "ScannerHangFixture.exe",
    "ResonanceMusicEditor.exe",
    "RealtimeEngineTests.exe",
    "SongProjectTests.exe"
)

$builtBinaries = foreach ($name in $expectedBinaries) {
    $binary = Get-ChildItem -LiteralPath $buildDir -Recurse -Filter $name |
        Where-Object { $_.FullName -match "\\$Configuration\\" } |
        Select-Object -First 1

    if ($null -eq $binary) {
        throw "The build completed, but $name was not found"
    }

    $binary
}

$binDir = Join-Path $projectRoot "bin"
New-Item -ItemType Directory -Path $binDir -Force | Out-Null

$productionBinaries = @(
    "ResonanceHostProbe.exe",
    "ResonancePluginScanner.exe",
    "ResonancePluginInventory.exe",
    "ResonanceMusicEditor.exe"
)

$packagedBinaries = foreach ($binary in $builtBinaries) {
    if ($productionBinaries -contains $binary.Name) {
        $destination = Join-Path $binDir $binary.Name
        Copy-Item -LiteralPath $binary.FullName -Destination $destination -Force
        Get-Item -LiteralPath $destination
    }
}

$packagedBinaries | Select-Object Name,FullName,Length,LastWriteTime
