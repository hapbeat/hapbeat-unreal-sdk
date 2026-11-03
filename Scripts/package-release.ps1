<#
.SYNOPSIS
    Builds the per-engine prebuilt release ZIPs of the Hapbeat SDK plugin.

.DESCRIPTION
    Maintainer script (end users never run it). For each engine it runs
    `RunUAT.bat BuildPlugin`, which compiles the plugin for that engine and
    writes an installed copy (Binaries + Source + Content + Config) whose
    .uplugin carries "EngineVersion": "5.x.0" and "Installed": true.
    Prebuilt binaries only load in the engine build they were compiled with,
    so there is one ZIP per engine minor version:

        <OutDir>/HapbeatSDK-<Version>-UE5.x.zip      plugin contents at the ZIP root
        <OutDir>/HapbeatSDK-<Version>-UE5.x/         the same files, unzipped
        <OutDir>/HapbeatSDK-<Version>-SHA256SUMS.txt  sha256sum format, all UE ZIPs of <Version>
        <OutDir>/logs/UE5.x-BuildPlugin.log

    BuildPlugin copies the whole source plugin folder into a host project, so
    the plugin is first staged into -WorkDir (a short path outside the repo)
    without .git / Binaries / Intermediate / Saved.

.PARAMETER Engines
    Engine minor versions to build ("5.4","5.6"). Default: every Epic
    Launcher UE_5.x install >= 5.3 listed in LauncherInstalled.dat.

.PARAMETER OutDir
    Output folder. Default: <repo>/Saved/ReleasePackages (gitignored).

.PARAMETER TargetPlatforms
    Passed to BuildPlugin -TargetPlatforms. Default "Win64"; e.g. "Win64+Android".
    Only the editor DLLs (Binaries/Win64) end up in the ZIP. Game-target builds
    (UnrealGame for each platform) are a compile check: their static libraries
    sit under Intermediate/, which is stripped, because a plugin under a
    project's Plugins/ folder is compiled from Source when a game is packaged.

.PARAMETER Version
    Version in the file names. Default: VersionName of HapbeatSDK.uplugin.

.PARAMETER IncludePdb
    Keep *.pdb files in the package (excluded by default; they are large).

.PARAMETER WorkDir
    Staging folder for BuildPlugin. Default: $env:TEMP\hbpkg (kept short:
    UBT intermediate paths get long).

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File Scripts/package-release.ps1
    powershell -ExecutionPolicy Bypass -File Scripts/package-release.ps1 -Engines 5.6 -TargetPlatforms Win64+Android
#>
[CmdletBinding()]
param(
    [string[]]$Engines,
    [string]$OutDir,
    [string]$TargetPlatforms = 'Win64',
    [string]$Version,
    [switch]$IncludePdb,
    [string]$WorkDir
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 2.0

$PluginName = 'HapbeatSDK'
$MinMinor = 3
$RepoRoot = Split-Path -Parent $PSScriptRoot
$SourcePlugin = Join-Path $RepoRoot "$PluginName.uplugin"
# Plugin folders copied into the staging tree. Everything else (.git, Binaries,
# Intermediate, Saved, docs, .github) stays out of the host project.
$StageItems = @("$PluginName.uplugin", 'Source', 'Content', 'Config', 'Resources', 'Scripts', 'README.md', 'CHANGELOG.md', 'LICENSE')

if (-not (Test-Path -LiteralPath $SourcePlugin)) { throw "Not found: $SourcePlugin" }
if (-not $OutDir) { $OutDir = Join-Path $RepoRoot 'Saved\ReleasePackages' }
if (-not $WorkDir) { $WorkDir = Join-Path $env:TEMP 'hbpkg' }
if (-not $Version) { $Version = (Get-Content -LiteralPath $SourcePlugin -Raw | ConvertFrom-Json).VersionName }
if (-not $Version) { throw "VersionName is empty in $SourcePlugin; pass -Version." }
$OutDir = [IO.Path]::GetFullPath($OutDir)
$WorkDir = [IO.Path]::GetFullPath($WorkDir)

function Get-LauncherEngines {
    # Returns @{ '5.6' = 'M:\...\UE_5.6'; ... } for Epic Launcher UE_5.x installs.
    $dat = Join-Path $env:ProgramData 'Epic\UnrealEngineLauncher\LauncherInstalled.dat'
    $map = @{}
    if (-not (Test-Path -LiteralPath $dat)) { return $map }
    foreach ($item in (Get-Content -LiteralPath $dat -Raw | ConvertFrom-Json).InstallationList) {
        if ($item.AppName -match '^UE_(5\.\d+)$') { $map[$Matches[1]] = $item.InstallLocation }
    }
    return $map
}

function Get-Minor([string]$EngineVersion) {
    if ($EngineVersion -notmatch '^5\.(\d+)$') { throw "Engine version must look like 5.x: '$EngineVersion'" }
    return [int]$Matches[1]
}

function Write-Zip([string]$SourceDir, [string]$ZipPath) {
    # ZipArchive with '/' entry names: Compress-Archive on Windows PowerShell 5.1
    # writes '\' separators, which other unzip tools treat as part of the name.
    Add-Type -AssemblyName System.IO.Compression
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    if (Test-Path -LiteralPath $ZipPath) { Remove-Item -LiteralPath $ZipPath -Force }
    $root = $SourceDir.TrimEnd('\') + '\'
    $zip = [IO.Compression.ZipFile]::Open($ZipPath, [IO.Compression.ZipArchiveMode]::Create)
    try {
        foreach ($file in Get-ChildItem -LiteralPath $SourceDir -Recurse -File) {
            $entry = $file.FullName.Substring($root.Length).Replace('\', '/')
            [void][IO.Compression.ZipFileExtensions]::CreateEntryFromFile($zip, $file.FullName, $entry, [IO.Compression.CompressionLevel]::Optimal)
        }
    }
    finally { $zip.Dispose() }
}

function Build-Engine([string]$EngineVersion, [string]$EngineRoot) {
    $tag = "UE$EngineVersion"
    $runUat = Join-Path $EngineRoot 'Engine\Build\BatchFiles\RunUAT.bat'
    if (-not (Test-Path -LiteralPath $runUat)) { throw "RunUAT.bat not found: $runUat" }

    $engineWork = Join-Path $WorkDir $EngineVersion
    $stageDir = Join-Path $engineWork "src\$PluginName"
    $pkgDir = Join-Path $engineWork 'pkg'
    if (Test-Path -LiteralPath $engineWork) { Remove-Item -LiteralPath $engineWork -Recurse -Force }
    New-Item -ItemType Directory -Path $stageDir -Force | Out-Null
    foreach ($item in $StageItems) {
        $src = Join-Path $RepoRoot $item
        if (Test-Path -LiteralPath $src) { Copy-Item -LiteralPath $src -Destination $stageDir -Recurse -Force }
    }

    $logDir = Join-Path $OutDir 'logs'
    New-Item -ItemType Directory -Path $logDir -Force | Out-Null
    $log = Join-Path $logDir "$tag-BuildPlugin.log"
    $uatArgs = @('BuildPlugin', "-Plugin=$(Join-Path $stageDir "$PluginName.uplugin")", "-Package=$pkgDir", "-TargetPlatforms=$TargetPlatforms")
    Write-Host "[$tag] $runUat $($uatArgs -join ' ')"
    Write-Host "[$tag] log: $log"

    # UAT writes progress to stderr too; keep native stderr from becoming a
    # terminating error under 'Stop' and judge success by the exit code.
    # Tee-Object on Windows PowerShell 5.1 writes UTF-16, so write the log as UTF-8 here.
    $writer = New-Object IO.StreamWriter($log, $false, (New-Object Text.UTF8Encoding($false)))
    $ErrorActionPreference = 'Continue'
    try { & $runUat @uatArgs 2>&1 | ForEach-Object { $line = "$_"; $writer.WriteLine($line); $line } | Out-Host }
    finally { $writer.Dispose() }
    $exit = $LASTEXITCODE
    $ErrorActionPreference = 'Stop'
    if ($exit -ne 0) { throw "BuildPlugin failed (exit $exit). See $log" }

    # BuildPlugin keeps the UHT-generated headers under Intermediate/; the
    # prebuilt binaries do not need them and a C++ project regenerates them.
    $intermediate = Join-Path $pkgDir 'Intermediate'
    if (Test-Path -LiteralPath $intermediate) { Remove-Item -LiteralPath $intermediate -Recurse -Force }
    if (-not $IncludePdb) { Get-ChildItem -LiteralPath $pkgDir -Recurse -File -Filter '*.pdb' | Remove-Item -Force }

    $descriptor = Get-Content -LiteralPath (Join-Path $pkgDir "$PluginName.uplugin") -Raw | ConvertFrom-Json
    if ($descriptor.EngineVersion -ne "$EngineVersion.0") {
        throw "Packaged .uplugin EngineVersion is '$($descriptor.EngineVersion)', expected '$EngineVersion.0'."
    }
    $dlls = @(Get-ChildItem -LiteralPath (Join-Path $pkgDir 'Binaries\Win64') -Filter '*.dll' -ErrorAction SilentlyContinue)
    if ($dlls.Count -eq 0) { throw "No Binaries/Win64/*.dll in the package." }

    $name = "$PluginName-$Version-$tag"
    $finalDir = Join-Path $OutDir $name
    if (Test-Path -LiteralPath $finalDir) { Remove-Item -LiteralPath $finalDir -Recurse -Force }
    Copy-Item -LiteralPath $pkgDir -Destination $finalDir -Recurse
    $zipPath = Join-Path $OutDir "$name.zip"
    Write-Zip $finalDir $zipPath
    return $zipPath
}

# --- engine selection -------------------------------------------------------
$installed = Get-LauncherEngines
# `powershell -File ... -Engines 5.4,5.6` passes one string; accept commas too.
$Engines = @($Engines | ForEach-Object { $_ -split ',' } | ForEach-Object { $_.Trim() } | Where-Object { $_ })
if ($Engines.Count -eq 0) {
    $Engines = @($installed.Keys | Where-Object { (Get-Minor $_) -ge $MinMinor } | Sort-Object { Get-Minor $_ })
    if ($Engines.Count -eq 0) { throw "No Epic Launcher UE 5.$MinMinor+ install found; pass -Engines." }
}
foreach ($e in $Engines) {
    if ((Get-Minor $e) -lt $MinMinor) { throw "UE $e is below the supported 5.$MinMinor." }
    if (-not $installed.ContainsKey($e)) { throw "UE $e is not listed in LauncherInstalled.dat." }
}

$dirty = $null
try { $dirty = git -C $RepoRoot status --porcelain 2>$null } catch { }
if ($dirty) { Write-Warning "The working tree has uncommitted changes; they will be packaged as-is." }

New-Item -ItemType Directory -Path $OutDir -Force | Out-Null
Write-Host "HapbeatSDK $Version | engines: $($Engines -join ', ') | platforms: $TargetPlatforms | out: $OutDir"

# --- build ------------------------------------------------------------------
$results = @()
foreach ($e in $Engines) {
    $sw = [Diagnostics.Stopwatch]::StartNew()
    try {
        $zip = Build-Engine $e $installed[$e]
        $results += [pscustomobject]@{ Engine = "UE$e"; Result = 'OK'; SizeMB = [math]::Round((Get-Item -LiteralPath $zip).Length / 1MB, 1); Minutes = [math]::Round($sw.Elapsed.TotalMinutes, 1); Detail = $zip }
    }
    catch {
        $results += [pscustomobject]@{ Engine = "UE$e"; Result = 'FAILED'; SizeMB = $null; Minutes = [math]::Round($sw.Elapsed.TotalMinutes, 1); Detail = "$_" }
    }
}

# --- checksums over every UE ZIP of this version in OutDir ------------------
$zips = @(Get-ChildItem -LiteralPath $OutDir -File -Filter "$PluginName-$Version-UE*.zip" | Sort-Object Name)
if ($zips.Count -gt 0) {
    $sums = Join-Path $OutDir "$PluginName-$Version-SHA256SUMS.txt"
    $lines = foreach ($z in $zips) { '{0}  {1}' -f (Get-FileHash -LiteralPath $z.FullName -Algorithm SHA256).Hash.ToLowerInvariant(), $z.Name }
    [IO.File]::WriteAllText($sums, (($lines -join "`n") + "`n"), (New-Object Text.UTF8Encoding($false)))
    Write-Host "Checksums: $sums"
}

$results | Format-Table -AutoSize -Wrap | Out-String -Width 200 | Write-Host
if (@($results | Where-Object { $_.Result -ne 'OK' }).Count -gt 0) { exit 1 }
exit 0
