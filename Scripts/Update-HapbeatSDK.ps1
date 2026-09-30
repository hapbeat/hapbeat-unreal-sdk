<#
.SYNOPSIS
    Installs or updates the prebuilt Hapbeat SDK plugin in an Unreal project.

.DESCRIPTION
    Downloads HapbeatSDK-<version>-UE<5.x>.zip for the project's engine from
    GitHub Releases, verifies it against HapbeatSDK-<version>-SHA256SUMS.txt
    when the release has one, and replaces <ProjectRoot>/Plugins/HapbeatSDK.

    The old plugin folder is never deleted: it is moved to
    <ProjectRoot>/Saved/HapbeatSDK-backup-<old version>-<timestamp> (outside
    Plugins/, so Unreal does not load two copies). Roll back by moving it back.

    Close the Unreal Editor first. A git clone (Plugins/HapbeatSDK/.git) is
    refused: update it with `git pull` instead.

.PARAMETER ProjectRoot
    Folder containing the .uproject. Default: the first folder above this
    script that contains one.

.PARAMETER Engine
    Engine minor version (e.g. 5.6). Default: the .uproject EngineAssociation;
    required when that is a source-build GUID.

.PARAMETER Version
    Release to install (v0.2.0 or 0.2.0). Default: the latest release.

.PARAMETER SourceZip
    Install from a local ZIP instead of downloading. A
    HapbeatSDK-*-SHA256SUMS.txt next to it is used for verification.

.PARAMETER SourceDir
    Install from a local folder of release assets (for example the
    Saved/ReleasePackages output of package-release.ps1) instead of downloading.

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File Plugins/HapbeatSDK/Scripts/Update-HapbeatSDK.ps1
    powershell -ExecutionPolicy Bypass -File Plugins/HapbeatSDK/Scripts/Update-HapbeatSDK.ps1 -Version v0.2.0 -WhatIf
#>
[CmdletBinding(SupportsShouldProcess = $true, DefaultParameterSetName = 'Release')]
param(
    [string]$ProjectRoot,
    [string]$Engine,
    [Parameter(ParameterSetName = 'Release')][string]$Version,
    [Parameter(ParameterSetName = 'Zip', Mandatory = $true)][string]$SourceZip,
    [Parameter(ParameterSetName = 'Dir', Mandatory = $true)][string]$SourceDir
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 2.0

# -WhatIf is a dry run of the final replace only. Clear the preference so the
# download, checksum and extraction below still run (Get-FileHash, Remove-Item
# etc. would otherwise all turn into no-ops).
$DryRun = [bool]$WhatIfPreference
$WhatIfPreference = $false

$PluginName = 'HapbeatSDK'
$Repo = 'hapbeat/hapbeat-unreal-sdk'

function Find-ProjectRoot([string]$Start) {
    $dir = $Start
    while ($dir) {
        if (@(Get-ChildItem -LiteralPath $dir -Filter '*.uproject' -File -ErrorAction SilentlyContinue).Count -gt 0) { return $dir }
        $parent = Split-Path -Parent $dir
        if ($parent -eq $dir) { break }
        $dir = $parent
    }
    return $null
}

function Get-PluginVersion([string]$PluginDir) {
    $uplugin = Join-Path $PluginDir "$PluginName.uplugin"
    if (-not (Test-Path -LiteralPath $uplugin)) { return 'unknown' }
    try { return (Get-Content -LiteralPath $uplugin -Raw | ConvertFrom-Json).VersionName } catch { return 'unknown' }
}

function Get-ExpectedHash([string]$SumsPath, [string]$FileName) {
    foreach ($line in Get-Content -LiteralPath $SumsPath) {
        if ($line -match '^([0-9a-fA-F]{64})\s+\*?(.+?)\s*$' -and $Matches[2] -eq $FileName) { return $Matches[1].ToLowerInvariant() }
    }
    throw "$FileName is not listed in $SumsPath."
}

function Assert-Hash([string]$ZipPath, [string]$SumsPath) {
    if (-not $SumsPath) {
        Write-Warning 'No SHA256SUMS file available; skipping checksum verification.'
        return
    }
    $expected = Get-ExpectedHash $SumsPath (Split-Path -Leaf $ZipPath)
    $actual = (Get-FileHash -LiteralPath $ZipPath -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($actual -ne $expected) { throw "SHA256 mismatch for $(Split-Path -Leaf $ZipPath): expected $expected, got $actual." }
    Write-Host "SHA256 OK: $actual"
}

function Invoke-Download([string]$Url, [string]$OutFile) {
    Invoke-WebRequest -Uri $Url -OutFile $OutFile -UseBasicParsing -UserAgent 'Update-HapbeatSDK' -Headers @{ Accept = 'application/octet-stream' }
}

# --- project and engine -----------------------------------------------------
if (-not $ProjectRoot) { $ProjectRoot = Find-ProjectRoot $PSScriptRoot }
if (-not $ProjectRoot) { throw 'No .uproject found above this script. Pass -ProjectRoot <folder with the .uproject>.' }
$ProjectRoot = (Resolve-Path -LiteralPath $ProjectRoot).ProviderPath
$uprojects = @(Get-ChildItem -LiteralPath $ProjectRoot -Filter '*.uproject' -File)
if ($uprojects.Count -ne 1) { throw "Expected exactly one .uproject in $ProjectRoot, found $($uprojects.Count)." }
$uproject = $uprojects[0].FullName

if (-not $Engine) {
    $association = [string](Get-Content -LiteralPath $uproject -Raw | ConvertFrom-Json).EngineAssociation
    if ($association -match '^(5\.\d+)') { $Engine = $Matches[1] }
    else { throw "EngineAssociation '$association' is not a launcher version (source build?). Pass -Engine 5.x." }
}
if ($Engine -notmatch '^5\.\d+$') { throw "-Engine must look like 5.x: '$Engine'" }
$assetName = "^$PluginName-.+-UE$([regex]::Escape($Engine))\.zip$"

$pluginsDir = Join-Path $ProjectRoot 'Plugins'
$pluginDir = Join-Path $pluginsDir $PluginName
Write-Host "Project: $uproject (UE $Engine)"

# --- safety checks ------------------------------------------------------------
if (Test-Path -LiteralPath (Join-Path $pluginDir '.git')) {
    throw "$pluginDir is a git clone. Update it with 'git pull' (and rebuild) instead of this script."
}
# Match on the .uproject file name: the editor may have been started with a
# relative or forward-slash path. A same-named project elsewhere also refuses,
# which errs on the safe side.
$uprojectName = [IO.Path]::GetFileName($uproject)
$editors = @(Get-CimInstance Win32_Process -Filter "Name LIKE 'UnrealEditor%'" -ErrorAction SilentlyContinue)
foreach ($p in $editors) {
    if ($p.CommandLine -and $p.CommandLine.IndexOf($uprojectName, [StringComparison]::OrdinalIgnoreCase) -ge 0) {
        throw "Unreal Editor (PID $($p.ProcessId)) has this project open. Close it and run again."
    }
}
if ($editors.Count -gt 0) { Write-Warning "Unreal Editor is running (not with this project, as far as its command line shows). Make sure $uprojectName is closed." }

# --- fetch the ZIP --------------------------------------------------------------
$workDir = Join-Path $ProjectRoot "Saved\$PluginName-update"
if (Test-Path -LiteralPath $workDir) { Remove-Item -LiteralPath $workDir -Recurse -Force }
New-Item -ItemType Directory -Path $workDir -Force | Out-Null

$sums = $null
if ($PSCmdlet.ParameterSetName -eq 'Zip') {
    $zip = (Resolve-Path -LiteralPath $SourceZip).ProviderPath
    $sums = Get-ChildItem -LiteralPath (Split-Path -Parent $zip) -Filter "$PluginName-*-SHA256SUMS.txt" -File |
        Where-Object { (Get-Content -LiteralPath $_.FullName) -match [regex]::Escape((Split-Path -Leaf $zip)) } |
        Select-Object -First 1 -ExpandProperty FullName
}
elseif ($PSCmdlet.ParameterSetName -eq 'Dir') {
    $dir = (Resolve-Path -LiteralPath $SourceDir).ProviderPath
    $found = @(Get-ChildItem -LiteralPath $dir -File | Where-Object { $_.Name -match $assetName } | Sort-Object LastWriteTime -Descending)
    if ($found.Count -eq 0) { throw "No $PluginName-*-UE$Engine.zip in $dir." }
    $zip = $found[0].FullName
    $sumsName = $found[0].Name -replace "-UE$([regex]::Escape($Engine))\.zip$", '-SHA256SUMS.txt'
    if (Test-Path -LiteralPath (Join-Path $dir $sumsName)) { $sums = Join-Path $dir $sumsName }
}
else {
    [Net.ServicePointManager]::SecurityProtocol = [Net.ServicePointManager]::SecurityProtocol -bor [Net.SecurityProtocolType]::Tls12
    $ProgressPreference = 'SilentlyContinue'
    if ($Version) {
        $tag = if ($Version.StartsWith('v')) { $Version } else { "v$Version" }
        $api = "https://api.github.com/repos/$Repo/releases/tags/$tag"
    }
    else { $api = "https://api.github.com/repos/$Repo/releases/latest" }
    Write-Host "Querying $api"
    $release = Invoke-RestMethod -Uri $api -UseBasicParsing -UserAgent 'Update-HapbeatSDK'
    $asset = @($release.assets | Where-Object { $_.name -match $assetName }) | Select-Object -First 1
    if (-not $asset) {
        throw "Release $($release.tag_name) has no prebuilt ZIP for UE $Engine. Use the source install (git clone) for this engine; see $($release.html_url)"
    }
    $zip = Join-Path $workDir $asset.name
    Write-Host "Downloading $($asset.name) ($([math]::Round($asset.size / 1MB, 1)) MB) from $($release.tag_name)"
    Invoke-Download $asset.browser_download_url $zip
    $sumsAsset = @($release.assets | Where-Object { $_.name -like "$PluginName-*-SHA256SUMS.txt" }) | Select-Object -First 1
    if ($sumsAsset) {
        $sums = Join-Path $workDir $sumsAsset.name
        Invoke-Download $sumsAsset.browser_download_url $sums
    }
}
Assert-Hash $zip $sums

# --- extract and validate -------------------------------------------------------
$extractDir = Join-Path $workDir 'extract'
Expand-Archive -LiteralPath $zip -DestinationPath $extractDir -Force
$newUplugin = Get-ChildItem -LiteralPath $extractDir -Filter "$PluginName.uplugin" -File -Recurse | Select-Object -First 1
if (-not $newUplugin) { throw "$(Split-Path -Leaf $zip) does not contain $PluginName.uplugin." }
$newDir = $newUplugin.DirectoryName
$newDescriptor = Get-Content -LiteralPath $newUplugin.FullName -Raw | ConvertFrom-Json
$newEngine = [string]$newDescriptor.EngineVersion
if ($newEngine -and -not $newEngine.StartsWith("$Engine.")) {
    throw "The ZIP was built for UE $newEngine, but the project uses UE $Engine."
}
$newVersion = $newDescriptor.VersionName

# --- replace --------------------------------------------------------------------
$oldVersion = $null
$backupDir = $null
if (Test-Path -LiteralPath $pluginDir) {
    $oldVersion = Get-PluginVersion $pluginDir
    $backupDir = Join-Path $ProjectRoot ("Saved\$PluginName-backup-$oldVersion-" + (Get-Date -Format 'yyyyMMdd-HHmmss'))
}

$action = if ($oldVersion) { "Replace $PluginName $oldVersion with $newVersion (old folder -> $backupDir)" } else { "Install $PluginName $newVersion" }
if ($DryRun) {
    Write-Host "What if: $action"
    Write-Host "Dry run: nothing changed. The verified ZIP and its extracted files are in $workDir."
    return
}

# A process whose current directory is inside the plugin folder would block the move.
if ([Environment]::CurrentDirectory.StartsWith($pluginDir, [StringComparison]::OrdinalIgnoreCase)) {
    [Environment]::CurrentDirectory = $ProjectRoot
    Set-Location -LiteralPath $ProjectRoot
}

New-Item -ItemType Directory -Path $pluginsDir -Force | Out-Null
if ($backupDir) {
    try { Move-Item -LiteralPath $pluginDir -Destination $backupDir }
    catch { throw "Could not move $pluginDir aside (a file in it is probably open; close the editor / Visual Studio / Explorer windows). Nothing was changed. $_" }
}
try { Move-Item -LiteralPath $newDir -Destination $pluginDir }
catch {
    if ($backupDir) { Move-Item -LiteralPath $backupDir -Destination $pluginDir }
    throw "Could not place the new plugin; the previous folder was restored. $_"
}
Remove-Item -LiteralPath $workDir -Recurse -Force

Write-Host ''
Write-Host "$action : done."
Write-Host "  Plugin: $pluginDir"
if ($backupDir) {
    Write-Host "  Backup: $backupDir"
    Write-Host '  Roll back (editor closed):'
    Write-Host "    Move-Item '$pluginDir' '$ProjectRoot\Saved\$PluginName-rejected-$newVersion'"
    Write-Host "    Move-Item '$backupDir' '$pluginDir'"
}
