param(
    [Parameter(Mandatory = $true)]
    [string]$Version,
    [string]$Title = "",
    [string]$Notes = "",
    [string]$CommitMessage = "",
    [string]$GameDir = "",
    [switch]$OverwriteIni,
    [switch]$SkipRelease,
    [switch]$Clobber,
    [switch]$DryRun
)

$ErrorActionPreference = "Stop"
$RootDir = $PSScriptRoot
$DistDir = Join-Path $RootDir "dist"

# 1. Build all mods, deploy to local modloader, and package release ZIPs
$buildArgs = @("-NoProfile", "-ExecutionPolicy", "Bypass", "-File", (Join-Path $RootDir "build-all.ps1"), "-Version", $Version)
if ($OverwriteIni) { $buildArgs += "-OverwriteIni" }
if (-not [string]::IsNullOrWhiteSpace($GameDir)) { $buildArgs += @("-GameDir", $GameDir) }

Write-Host "=== [1/3] Build, Modloader Deploy & Package ($Version) ===" -ForegroundColor Cyan
& powershell @buildArgs
if ($LASTEXITCODE -ne 0) {
    throw "build-all.ps1 failed with exit code $LASTEXITCODE"
}

$Zips = @(Get-ChildItem $DistDir -Filter "*-$Version.zip" | ForEach-Object { $_.FullName })
if ($Zips.Count -eq 0) {
    throw "No release ZIPs found in $DistDir for version $Version"
}

# 2. Commit (if requested) and push to GitHub
Write-Host "=== [2/3] Git Commit & Push ===" -ForegroundColor Cyan
$status = & git -C $RootDir status --porcelain
if (-not [string]::IsNullOrWhiteSpace(($status -join ""))) {
    if ([string]::IsNullOrWhiteSpace($CommitMessage)) {
        throw "Working tree has uncommitted changes. Commit them first or pass -CommitMessage '...'."
    }
    if ($DryRun) {
        Write-Host "[DRY-RUN] git add -A && git commit -m `"$CommitMessage`"" -ForegroundColor Yellow
    } else {
        & git -C $RootDir add -A
        & git -C $RootDir commit -m $CommitMessage
        if ($LASTEXITCODE -ne 0) { throw "git commit failed" }
    }
}

if ($DryRun) {
    Write-Host "[DRY-RUN] git push origin main" -ForegroundColor Yellow
} else {
    & git -C $RootDir push origin main
    if ($LASTEXITCODE -ne 0) { throw "git push failed" }
}

# 3. Publish GitHub Release
if (-not $SkipRelease) {
    Write-Host "=== [3/3] GitHub Release ($Version) ===" -ForegroundColor Cyan
    $ReleaseTitle = if ([string]::IsNullOrWhiteSpace($Title)) { $Version } else { $Title }
    $ReleaseNotes = if ([string]::IsNullOrWhiteSpace($Notes)) { "Release $Version" } else { $Notes }

    $prevErr = $ErrorActionPreference
    $ErrorActionPreference = "Continue"
    & gh release view $Version 2>$null | Out-Null
    $releaseExists = ($LASTEXITCODE -eq 0)
    $ErrorActionPreference = $prevErr

    if ($DryRun) {
        if ($releaseExists) {
            Write-Host "[DRY-RUN] gh release upload $Version $($Zips.Count) zip(s) --clobber" -ForegroundColor Yellow
        } else {
            Write-Host "[DRY-RUN] gh release create $Version $($Zips.Count) zip(s) --title `"$ReleaseTitle`" --notes `"$ReleaseNotes`"" -ForegroundColor Yellow
        }
    } elseif ($releaseExists) {
        if (-not $Clobber) {
            throw "GitHub release '$Version' already exists. Pass -Clobber to overwrite its ZIP assets."
        }
        & gh release upload $Version @Zips --clobber
        if ($LASTEXITCODE -ne 0) { throw "gh release upload failed" }
    } else {
        & gh release create $Version @Zips --title $ReleaseTitle --notes $ReleaseNotes
        if ($LASTEXITCODE -ne 0) { throw "gh release create failed" }
    }
}

Write-Host "=== Release $Version deployed to modloader and delivered to GitHub! ===" -ForegroundColor Green
