<#
.SYNOPSIS
Download ntdll.dll builds and their PDBs into a local single-tier symbol store.

.DESCRIPTION
Reads the Winbindex metadata of ntdll.dll and selects the builds of Windows 10 22H2 (19045),
Windows 11 23H2 (22631) and Windows 11 24H2 (26100) for x64 (System32) and x86 (SysWOW64).
Builds that share TimeDateStamp and SizeOfImage on one architecture are downloaded once.
Entries without a timestamp or virtual size are skipped.

Each PE comes from the Microsoft symbol server into <Store>\ntdll.dll\<TimeDateStamp><SizeOfImage>\ntdll.dll;
its TimeDateStamp and SizeOfImage must equal the Winbindex values, otherwise the file is discarded and the
build fails. The PDB key is taken from the RSDS record of the downloaded PE and the PDB lands in
<Store>\<PDB name>\<GUID><Age>\<PDB name> (ntdll.pdb for x64, wntdll.pdb for x86). That is the layout the library searches through
_NT_SYMBOL_PATH=srv*<Store>.

Existing files are skipped. A download goes to a temporary name and is renamed on success.
The exit code is nonzero when any build failed.

.EXAMPLE
./scripts/Get-NtdllBuilds.ps1 -Store D:\symbols

.EXAMPLE
./scripts/Get-NtdllBuilds.ps1 -Store D:\symbols -Release 26100 -Limit 3
#>
[CmdletBinding()]
param(
    # Root of the symbol store; created when missing
    [Parameter(Mandatory)] [string] $Store,
    # Releases to download
    [ValidateSet('19045', '22631', '26100')] [string[]] $Release = @('19045', '22631', '26100'),
    # Newest N builds per release and architecture; all builds when omitted
    [ValidateRange(1, [int]::MaxValue)] [int] $Limit
)

$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'

$winbindexUrl = 'https://winbindex.m417z.com/data/by_filename_compressed/ntdll.dll.json.gz'
$symbolServer = 'https://msdl.microsoft.com/download/symbols'

$http = [System.Net.Http.HttpClient]::new()
$gzip = [System.IO.Compression.GZipStream]::new($http.GetStreamAsync($winbindexUrl).GetAwaiter().GetResult(), [System.IO.Compression.CompressionMode]::Decompress)
$index = [System.IO.StreamReader]::new($gzip).ReadToEnd() | ConvertFrom-Json -AsHashtable

# --- selection ---
# Winbindex sets per release; 22631 shares the 22H2 and 23H2 sets and only takes builds from the 2428 update on
$releaseSets = @{ '19045' = , '22H2'; '22631' = '11-22H2', '11-23H2'; '26100' = , '11-24H2' }
$minVersion = @{ '22631' = [version]'10.0.22621.2428' }
$machineArch = @{ 34404 = 'x64'; 332 = 'x86' }

$selected = foreach ($entry in $index.Values) {
    $info = $entry.fileInfo
    $arch = $machineArch[[int]$info.machineType]
    if (-not $arch -or -not $info.timestamp -or -not $info.virtualSize) { continue }

    # Some entries lack fileInfo.version; the assembly identity of the update carries it
    $versionText = $info.version
    if (-not $versionText) {
        $versionText = $entry.windowsVersions.Values.Values.assemblies.Values.assemblyIdentity.version | Select-Object -First 1
    }
    if (-not $versionText) { continue }
    $version = [version]($versionText -split ' ')[0]

    foreach ($rel in $Release) {
        if (-not ($entry.windowsVersions.Keys | Where-Object { $_ -in $releaseSets[$rel] })) { continue }
        if ($minVersion[$rel] -and $version -lt $minVersion[$rel]) { continue }

        [pscustomobject]@{
            Release = $rel
            Arch    = $arch
            Version = $version
            Key     = '{0:X8}{1:X}' -f [uint32]$info.timestamp, [uint32]$info.virtualSize
        }
    }
}

# One build per key and architecture, newest version first; -Limit counts those builds
$selected = $selected | Sort-Object Release, Arch, @{ Expression = 'Version'; Descending = $true } |
    Group-Object Release, Arch | ForEach-Object {
        $seen = [System.Collections.Generic.HashSet[string]]::new()
        $builds = $_.Group | Where-Object { $seen.Add($_.Key) }
        if ($Limit) { $builds | Select-Object -First $Limit } else { $builds }
    }
# --- end selection ---

# Returns TimeDateStamp, SizeOfImage and the PDB name and key (GUID and Age) from the RSDS record of a PE file
function Read-PeInfo([string] $Path) {
    $bytes = [System.IO.File]::ReadAllBytes($Path)
    $pe = [BitConverter]::ToInt32($bytes, 0x3C)
    if ([BitConverter]::ToUInt32($bytes, $pe) -ne 0x4550) { throw 'not a PE file' }

    $sectionCount = [BitConverter]::ToUInt16($bytes, $pe + 6)
    $optional = $pe + 24
    $isPe32Plus = [BitConverter]::ToUInt16($bytes, $optional) -eq 0x20B
    $directories = $optional + $(if ($isPe32Plus) { 112 } else { 96 })
    $debugRva = [BitConverter]::ToUInt32($bytes, $directories + 6 * 8)
    $debugSize = [BitConverter]::ToUInt32($bytes, $directories + 6 * 8 + 4)

    # The debug directory is addressed by RVA, so map it through the section table
    $sections = $optional + [BitConverter]::ToUInt16($bytes, $pe + 20)
    $debugOffset = 0
    for ($i = 0; $i -lt $sectionCount; $i++) {
        $s = $sections + $i * 40
        $va = [BitConverter]::ToUInt32($bytes, $s + 12)
        $rawSize = [BitConverter]::ToUInt32($bytes, $s + 16)
        if ($debugRva -ge $va -and $debugRva -lt $va + $rawSize) {
            $debugOffset = $debugRva - $va + [BitConverter]::ToUInt32($bytes, $s + 20)
        }
    }

    $pdbKey = $null
    $pdbName = $null
    for ($i = 0; $debugOffset -and $i -lt $debugSize / 28; $i++) {
        $d = $debugOffset + $i * 28
        $record = [BitConverter]::ToUInt32($bytes, $d + 24)
        if ([BitConverter]::ToUInt32($bytes, $d + 12) -eq 2 -and [BitConverter]::ToUInt32($bytes, $record) -eq 0x53445352) {
            # Guid( byte[] ) reads the on-disk layout; 'N' prints Data1, Data2, Data3, Data4 as PDBHelper does
            $guid = [guid]::new([byte[]]$bytes[($record + 4)..($record + 19)])
            $pdbKey = '{0}{1:X}' -f $guid.ToString('N').ToUpperInvariant(), [BitConverter]::ToUInt32($bytes, $record + 20)

            # The name is a zero-terminated build path; the WOW64 x86 ntdll is wntdll.pdb, not ntdll.pdb
            $nameEnd = [Array]::IndexOf($bytes, [byte]0, $record + 24)
            $pdbName = [System.IO.Path]::GetFileName([System.Text.Encoding]::ASCII.GetString($bytes, $record + 24, $nameEnd - $record - 24))
            break
        }
    }

    [pscustomobject]@{
        TimeDateStamp = [BitConverter]::ToUInt32($bytes, $pe + 8)
        SizeOfImage   = [BitConverter]::ToUInt32($bytes, $optional + 56)
        PdbKey        = $pdbKey
        PdbName       = $pdbName
    }
}

# Downloads to a temporary name, runs the optional check on it and renames on success, so a rejected or
# interrupted download never leaves a file under the final name; returns whether a download happened
function Get-StoreFile([string] $Url, [string] $Path, [scriptblock] $Verify) {
    if (Test-Path -LiteralPath $Path) { return $false }

    New-Item -ItemType Directory -Force -Path (Split-Path $Path) | Out-Null
    $temp = "$Path.part"
    try {
        try { Invoke-WebRequest -Uri $Url -OutFile $temp } catch { throw "$Url : $($_.Exception.Message)" }
        if ($Verify) { & $Verify $temp }
        Move-Item -LiteralPath $temp -Destination $Path
    }
    finally {
        Remove-Item -LiteralPath $temp -ErrorAction SilentlyContinue
    }

    $true
}

$failed = 0
foreach ($build in $selected) {
    $label = '{0} {1} {2} {3}' -f $build.Release, $build.Arch, $build.Version, $build.Key
    try {
        $peFresh = Get-StoreFile "$symbolServer/ntdll.dll/$($build.Key)/ntdll.dll" (Join-Path $Store "ntdll.dll\$($build.Key)\ntdll.dll") {
            param($file)
            $header = Read-PeInfo $file
            $actual = '{0:X8}{1:X}' -f $header.TimeDateStamp, $header.SizeOfImage
            if ($actual -ne $build.Key) { throw "PE header $actual differs from Winbindex $($build.Key)" }
        }

        $info = Read-PeInfo (Join-Path $Store "ntdll.dll\$($build.Key)\ntdll.dll")
        if (-not $info.PdbKey) { throw 'no RSDS record in the debug directory' }

        $pdbFresh = Get-StoreFile "$symbolServer/$($info.PdbName)/$($info.PdbKey)/$($info.PdbName)" (Join-Path $Store "$($info.PdbName)\$($info.PdbKey)\$($info.PdbName)")

        '{0} pe={1} pdb={2} {3}' -f $label, $(if ($peFresh) { 'downloaded' } else { 'skipped' }), $(if ($pdbFresh) { 'downloaded' } else { 'skipped' }), $info.PdbKey
    }
    catch {
        $failed++
        Write-Host "$label FAILED: $($_.Exception.Message)"
    }
}

Write-Host "Builds: $(@($selected).Count), failed: $failed"
if ($failed) { exit 1 }
