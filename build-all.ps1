param(
    [string]$Version = "",
    [switch]$SkipZip,
    [switch]$NoDeploy,
    [switch]$Debug,
    [switch]$OverwriteIni,
    [string]$GameDir = "",
    [string[]]$Only = @()
)

$ErrorActionPreference = "Stop"
$RootDir = $PSScriptRoot
$DistDir = Join-Path $RootDir "dist"

if ([string]::IsNullOrWhiteSpace($Version)) {
    try {
        $tag = (& git -C $RootDir describe --tags --abbrev=0 2>$null)
        if ($LASTEXITCODE -eq 0 -and -not [string]::IsNullOrWhiteSpace($tag)) {
            $Version = $tag.Trim()
        } else {
            $Version = "dev"
        }
    } catch {
        $Version = "dev"
    }
}

$AllMods = @(
    @{ Folder = "ModDev_UIKit";       Name = "GTA5UIKit" },
    @{ Folder = "ModDev_GTA5Weapons"; Name = "GTA5AmmuNation" },
    @{ Folder = "ModDev_GTA5Garage";  Name = "GTA5GarageAndTuning" },
    @{ Folder = "ModDev_Inventory";   Name = "GTA5Inventory" },
    @{ Folder = "ModDev_Phone";       Name = "GTA5Phone" },
    @{ Folder = "ModDev_EarlyUnlock"; Name = "GTA5EarlyUnlock" }
)

if ($Only.Count -gt 0) {
    $Mods = @($AllMods | Where-Object { ($Only -contains $_.Name) -or ($Only -contains $_.Folder) })
    if ($Mods.Count -eq 0) {
        $valid = ($AllMods | ForEach-Object { $_.Name }) -join ", "
        throw "No matching mods for -Only ($($Only -join ', ')). Valid names: $valid"
    }
} else {
    $Mods = $AllMods
}

$deployLabel = if ($NoDeploy) { "NoDeploy" } else { "Auto-Deploy to modloader" }
Write-Host "=== Building $($Mods.Count) GTA SA ASI plugin(s) ($Version | $deployLabel) ===" -ForegroundColor Cyan

foreach ($m in $Mods) {
    $script = Join-Path $RootDir "$($m.Folder)\build.ps1"
    $buildArgs = @("-NoProfile", "-ExecutionPolicy", "Bypass", "-File", $script)
    if ($NoDeploy) { $buildArgs += "-NoDeploy" }
    if ($Debug) { $buildArgs += "-Debug" }
    if ($OverwriteIni) { $buildArgs += "-OverwriteIni" }
    if (-not [string]::IsNullOrWhiteSpace($GameDir)) { $buildArgs += @("-GameDir", $GameDir) }

    & powershell @buildArgs
    if ($LASTEXITCODE -ne 0) {
        throw "Failed to build $($m.Name)"
    }
}

if (-not $SkipZip) {
    if ($Only.Count -eq 0 -and (Test-Path $DistDir)) {
        Remove-Item -Recurse -Force $DistDir
    }
    New-Item -ItemType Directory -Force -Path $DistDir | Out-Null

    $BundleStage = Join-Path $DistDir "_bundle_stage"
    if (Test-Path $BundleStage) { Remove-Item -Recurse -Force $BundleStage }
    if ($Only.Count -eq 0) {
        New-Item -ItemType Directory -Force -Path $BundleStage | Out-Null
    }

    foreach ($m in $Mods) {
        $modStageRoot = Join-Path $DistDir "_stage_$($m.Name)"
        $modSubDir    = Join-Path $modStageRoot $m.Name
        New-Item -ItemType Directory -Force -Path $modSubDir | Out-Null

        $builtAsi = Join-Path $RootDir "$($m.Folder)\build\$($m.Name).asi"
        $defIni   = Join-Path $RootDir "$($m.Folder)\config\$($m.Name).ini"
        $readme   = Join-Path $RootDir "$($m.Folder)\README.md"

        Copy-Item -Force $builtAsi (Join-Path $modSubDir "$($m.Name).asi")
        if (Test-Path $defIni) {
            Copy-Item -Force $defIni (Join-Path $modSubDir "$($m.Name).ini")
        }
        if (Test-Path $readme) {
            Copy-Item -Force $readme (Join-Path $modSubDir "README.md")
        }

        $zipPath = Join-Path $DistDir "$($m.Name)-$Version.zip"
        Compress-Archive -Path $modSubDir -DestinationPath $zipPath -Force
        Write-Host "[PACK] Created $zipPath" -ForegroundColor Green

        if ($Only.Count -eq 0) {
            Copy-Item -Recurse -Force $modSubDir (Join-Path $BundleStage $m.Name)
        }
        Remove-Item -Recurse -Force $modStageRoot
    }

    if ($Only.Count -eq 0) {
        Copy-Item -Force (Join-Path $RootDir "README.md") (Join-Path $BundleStage "README.md")
        Copy-Item -Force (Join-Path $RootDir "LICENSE")   (Join-Path $BundleStage "LICENSE")

        $BundleZip = Join-Path $DistDir "GTASA-GTA5-Mods-Bundle-$Version.zip"
        Compress-Archive -Path (Join-Path $BundleStage "*") -DestinationPath $BundleZip -Force
        Remove-Item -Recurse -Force $BundleStage
        Write-Host "[PACK] Created $BundleZip" -ForegroundColor Green
    }
}

Write-Host "=== Done! ($($Mods.Count) ASI plugin(s) built cleanly) ===" -ForegroundColor Cyan
