param(
    [string]$Version = "v1.0.0",
    [switch]$SkipZip
)

$ErrorActionPreference = "Stop"
$RootDir = $PSScriptRoot
$DistDir = Join-Path $RootDir "dist"

$Mods = @(
    @{ Folder = "ModDev_GTA5Garage";  Name = "GTA5GarageAndTuning" },
    @{ Folder = "ModDev_GTA5Weapons"; Name = "GTA5AmmuNation" },
    @{ Folder = "ModDev_EarlyUnlock"; Name = "GTA5EarlyUnlock" }
)

Write-Host "=== Building all GTA SA GTA5-style ASI plugins ($Version) ===" -ForegroundColor Cyan

foreach ($m in $Mods) {
    $script = Join-Path $RootDir "$($m.Folder)\build.ps1"
    & $script -NoDeploy
    if ($LASTEXITCODE -ne 0) {
        throw "Failed to build $($m.Name)"
    }
}

if (-not $SkipZip) {
    if (Test-Path $DistDir) { Remove-Item -Recurse -Force $DistDir }
    New-Item -ItemType Directory -Force -Path $DistDir | Out-Null

    $BundleStage = Join-Path $DistDir "_bundle_stage"
    New-Item -ItemType Directory -Force -Path $BundleStage | Out-Null

    foreach ($m in $Mods) {
        $modStageRoot = Join-Path $DistDir "_stage_$($m.Name)"
        $modSubDir    = Join-Path $modStageRoot $m.Name
        New-Item -ItemType Directory -Force -Path $modSubDir | Out-Null

        $builtAsi = Join-Path $RootDir "$($m.Folder)\build\$($m.Name).asi"
        $defIni   = Join-Path $RootDir "$($m.Folder)\config\$($m.Name).ini"
        $readme   = Join-Path $RootDir "$($m.Folder)\README.md"

        Copy-Item -Force $builtAsi (Join-Path $modSubDir "$($m.Name).asi")
        Copy-Item -Force $defIni   (Join-Path $modSubDir "$($m.Name).ini")
        if (Test-Path $readme) {
            Copy-Item -Force $readme (Join-Path $modSubDir "README.md")
        }

        $zipPath = Join-Path $DistDir "$($m.Name)-$Version.zip"
        Compress-Archive -Path $modSubDir -DestinationPath $zipPath -Force
        Write-Host "[PACK] Created $zipPath" -ForegroundColor Green

        # Also stage into the All-in-One bundle
        Copy-Item -Recurse -Force $modSubDir (Join-Path $BundleStage $m.Name)
        Remove-Item -Recurse -Force $modStageRoot
    }

    Copy-Item -Force (Join-Path $RootDir "README.md") (Join-Path $BundleStage "README.md")
    Copy-Item -Force (Join-Path $RootDir "LICENSE")   (Join-Path $BundleStage "LICENSE")

    $BundleZip = Join-Path $DistDir "GTASA-GTA5-Mods-Bundle-$Version.zip"
    Compress-Archive -Path (Join-Path $BundleStage "*") -DestinationPath $BundleZip -Force
    Remove-Item -Recurse -Force $BundleStage
    Write-Host "[PACK] Created $BundleZip" -ForegroundColor Green
}

Write-Host "=== All 3 ASI plugins built & packaged cleanly! ===" -ForegroundColor Cyan
