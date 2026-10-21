<#
.SYNOPSIS
    Creates the DRAFT GitHub Release for a version and attaches the prebuilt ZIPs.

.DESCRIPTION
    Maintainer script, run after package-release.ps1. It does not create a tag
    and does not publish: the draft stays invisible to users and to
    Update-HapbeatSDK.ps1 (which reads releases/latest) until a maintainer
    publishes it on GitHub. Publishing creates the vX.Y.Z tag at -Target and
    triggers the Release workflow, which attaches the source ZIP.

    Requires the GitHub CLI (gh) logged in with write access, and node for the
    release notes (.github/scripts/extract-changelog.mjs).

.PARAMETER Version
    Version to release (0.2.1). Default: VersionName of HapbeatSDK.uplugin.

.PARAMETER Target
    Commit the tag will point to when the draft is published. Must already be on
    GitHub. Default: HEAD.

.PARAMETER PackageDir
    Folder with HapbeatSDK-<Version>-UE5.x.zip and the SHA256SUMS file.
    Default: <repo>/Saved/ReleasePackages.

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File Scripts/draft-release.ps1
#>
[CmdletBinding()]
param(
    [string]$Version,
    [string]$Target,
    [string]$PackageDir
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 2.0

$RepoRoot = Split-Path -Parent $PSScriptRoot
$Repo = 'hapbeat/hapbeat-unreal-sdk'
if (-not $Version) { $Version = (Get-Content -LiteralPath (Join-Path $RepoRoot 'HapbeatSDK.uplugin') -Raw | ConvertFrom-Json).VersionName }
$Version = $Version -replace '^v', ''
$Tag = "v$Version"
if (-not $PackageDir) { $PackageDir = Join-Path $RepoRoot 'Saved\ReleasePackages' }
if (-not $Target) { $Target = (git -C $RepoRoot rev-parse HEAD).Trim() }

$zips = @(Get-ChildItem -LiteralPath $PackageDir -Filter "HapbeatSDK-$Version-UE5.*.zip" -File -ErrorAction SilentlyContinue)
$sums = Join-Path $PackageDir "HapbeatSDK-$Version-SHA256SUMS.txt"
if ($zips.Count -eq 0) { throw "No HapbeatSDK-$Version-UE5.*.zip in $PackageDir. Run Scripts/package-release.ps1 first." }
if (-not (Test-Path -LiteralPath $sums)) { throw "Missing $sums. Run Scripts/package-release.ps1 first." }

# The tagged commit must be on GitHub, or the draft cannot be published.
# Native tools report "not found" on stderr; don't let that stop the script.
$ErrorActionPreference = 'Continue'
$onRemote = git -C $RepoRoot branch -r --contains $Target 2>&1
$remoteOk = ($LASTEXITCODE -eq 0) -and ("$onRemote".Trim() -ne '')
# Refuse to touch an existing release (published or draft) of this version.
gh release view $Tag --repo $Repo --json isDraft *> $null
$exists = ($LASTEXITCODE -eq 0)
$ErrorActionPreference = 'Stop'
if (-not $remoteOk) { throw "Commit $Target is not on any remote branch. Push it first." }
if ($exists) { throw "Release $Tag already exists on GitHub. Edit it there instead." }

Push-Location $RepoRoot
try {
    node .github/scripts/extract-changelog.mjs $Tag
    if ($LASTEXITCODE -ne 0) { throw "CHANGELOG.md has no [$Version] section." }
    $notes = Join-Path $RepoRoot 'release-notes.md'
    $assets = @($zips | ForEach-Object { $_.FullName }) + $sums
    gh release create $Tag --repo $Repo --draft --target $Target --title $Tag --notes-file $notes @assets
    if ($LASTEXITCODE -ne 0) { throw "gh release create failed." }
    Remove-Item -LiteralPath $notes
}
finally { Pop-Location }

Write-Host "Draft $Tag created at $Target with $($zips.Count) engine ZIP(s) + SHA256SUMS."
Write-Host "Check it on https://github.com/$Repo/releases and publish it there when verified."
