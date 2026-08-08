$projectRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$localRoot = Join-Path $projectRoot ".local"

function Resolve-ResonancePath([string]$EnvironmentName, [string]$DefaultPath) {
    $configured = [Environment]::GetEnvironmentVariable($EnvironmentName)
    if ([string]::IsNullOrWhiteSpace($configured)) {
        return [IO.Path]::GetFullPath($DefaultPath)
    }

    return [IO.Path]::GetFullPath($configured)
}

$juceDir = Resolve-ResonancePath "RESONANCE_JUCE_DIR" (Join-Path $localRoot "deps\JUCE")
$buildDir = Resolve-ResonancePath "RESONANCE_BUILD_DIR" (Join-Path $localRoot "build")
$surgeVst3 = Resolve-ResonancePath "RESONANCE_SURGE_VST3" `
    (Join-Path $localRoot "surge-xt-portable\Surge Synth Team\Surge XT.vst3")
