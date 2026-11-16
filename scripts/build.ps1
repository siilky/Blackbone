<#
.SYNOPSIS
Configure, build, and optionally test BlackBone with CMake presets.

.DESCRIPTION
Activates the Visual Studio developer shell unless one is already active, configures
src/ with a preset into build/<arch>-<preset>, builds one configuration, and runs the
native tests through vstest.console.exe with /Blame, which records the test that was
running when a test host crashes (build/<arch>-<preset>/TestResults/**/Sequence.xml).

.EXAMPLE
./scripts/build.ps1 -Test
./scripts/build.ps1 -Arch x64,x86 -Preset static-mt,static-md,dll -Test
#>
param(
    # Target architectures; each one builds in its own developer shell process
    [ValidateSet('x64', 'x86')] [string[]] $Arch = 'x64',
    # CMake presets from src/CMakePresets.json
    [ValidateSet('static-mt', 'static-md', 'dll')] [string[]] $Preset = 'static-mt',
    # Build configuration: Release, Debug, or RelWithDebInfo
    [string] $Config = 'Release',
    # Run the native tests after the build
    [switch] $Test,
    # vstest.console.exe /TestCaseFilter expression, e.g. 'Name!=CallLoop'
    [string] $TestFilter,
    # Run the tests that need the BlackBone driver loaded; they are ignored otherwise
    [switch] $DriverTests
)

# A developer shell fixes the target architecture of its process: activating another one in
# the same process keeps the first, and child processes inherit it. So every combination runs
# in a child process, and a shell that is already active can build only its own architecture.
$otherArch = $Arch | Where-Object { $env:VSCMD_ARG_TGT_ARCH -and $_ -ne $env:VSCMD_ARG_TGT_ARCH }
if ($otherArch) {
    Write-Error "The active developer shell targets $env:VSCMD_ARG_TGT_ARCH and cannot build $($otherArch -join ', '); run from a shell without a developer environment"
    exit 1
}

if ($Arch.Count -gt 1 -or $Preset.Count -gt 1) {
    $failed = @()
    foreach ($a in $Arch) {
        foreach ($p in $Preset) {
            $childArgs = @('-NoProfile', '-File', $PSCommandPath, '-Arch', $a, '-Preset', $p, '-Config', $Config)
            if ($Test) { $childArgs += '-Test' }
            if ($TestFilter) { $childArgs += '-TestFilter', $TestFilter }
            if ($DriverTests) { $childArgs += '-DriverTests' }

            & pwsh @childArgs
            if ($LASTEXITCODE) { $failed += "$a-$p" }
        }
    }

    if ($failed) { Write-Host "Failed: $($failed -join ', ')"; exit 1 }
    exit 0
}

$Arch = $Arch[0]
$Preset = $Preset[0]

if (-not $env:VSCMD_ARG_TGT_ARCH) {
    $vs = & "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe" -latest -property installationPath
    $devArch = if ($Arch -eq 'x86') { 'x86' } else { 'amd64' }
    & "$vs\Common7\Tools\Launch-VsDevShell.ps1" -Arch $devArch -HostArch amd64 -SkipAutomaticLocation 2>$null | Out-Null

    if (-not $env:VSCMD_ARG_TGT_ARCH) {
        Write-Error 'Visual Studio developer shell activation failed'
        exit 1
    }
}

# A missing cmake or vstest.console.exe must fail the run instead of leaving $LASTEXITCODE unset.
$ErrorActionPreference = 'Stop'

$root = Split-Path $PSScriptRoot
$buildDir = "$root/build/$Arch-$Preset"
$skipDriver = if ($DriverTests) { 'OFF' } else { 'ON' }

cmake -S "$root/src" --preset $Preset "-DBLACKBONE_TEST_SKIP_DRIVER=$skipDriver"
if ($LASTEXITCODE) { exit $LASTEXITCODE }

cmake --build $buildDir --config $Config
if ($LASTEXITCODE) { exit $LASTEXITCODE }

if ($Test) {
    $testArgs = @("$buildDir/$Config/BlackBoneTest.dll", '/Blame', "/ResultsDirectory:$buildDir/TestResults")
    if ($TestFilter) { $testArgs += "/TestCaseFilter:$TestFilter" }

    vstest.console.exe @testArgs
    exit $LASTEXITCODE
}
