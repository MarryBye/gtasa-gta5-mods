param(
    [switch]$NoDeploy,
    [switch]$Debug,
    [switch]$OverwriteIni,
    [string]$GameDir = ""
)

$ErrorActionPreference = "Stop"

$ProjectDir   = $PSScriptRoot
$WorkspaceDir = Split-Path -Parent $ProjectDir
$BuildDir     = Join-Path $ProjectDir "build"
$Compiler     = "C:\msys64\mingw32\bin\g++.exe"
$ObjDump      = "C:\msys64\mingw32\bin\objdump.exe"
$ModName      = "GTA5Inventory"
$OutAsi       = Join-Path $BuildDir "$ModName.asi"

# Resolve GTA SA directory: -GameDir -> $env:GTASA_DIR -> local.config.ps1 -> legacy parent\modloader
$ResolvedGameDir = $GameDir
if ([string]::IsNullOrWhiteSpace($ResolvedGameDir) -and -not [string]::IsNullOrWhiteSpace($env:GTASA_DIR)) {
    $ResolvedGameDir = $env:GTASA_DIR
}
$LocalConfig = Join-Path $WorkspaceDir "local.config.ps1"
if (Test-Path $LocalConfig) {
    $cfg = & {
        $GameDir = $null
        $Compiler = $null
        . $LocalConfig
        [PSCustomObject]@{ GameDir = $GameDir; Compiler = $Compiler }
    }
    if ([string]::IsNullOrWhiteSpace($ResolvedGameDir) -and -not [string]::IsNullOrWhiteSpace($cfg.GameDir)) {
        $ResolvedGameDir = $cfg.GameDir
    }
    if (-not [string]::IsNullOrWhiteSpace($cfg.Compiler)) {
        $Compiler = $cfg.Compiler
        $ObjDump  = Join-Path (Split-Path -Parent $Compiler) "objdump.exe"
    }
}
if ([string]::IsNullOrWhiteSpace($ResolvedGameDir) -and (Test-Path (Join-Path $WorkspaceDir "modloader"))) {
    $ResolvedGameDir = $WorkspaceDir
}

if (-not (Test-Path $Compiler)) {
    throw "32-bit compiler not found at $Compiler. Install mingw-w64-i686-gcc via MSYS2."
}

New-Item -ItemType Directory -Force -Path $BuildDir | Out-Null
$env:PATH = "$(Split-Path -Parent $Compiler);$env:PATH"

$Sources = @(Get-ChildItem (Join-Path $ProjectDir "src") -Filter "*.cpp" | ForEach-Object { $_.FullName })
if ($Sources.Count -eq 0) {
    throw "No .cpp source files found in src/"
}

$Flags = @(
    "-std=c++20",
    "-m32",
    "-shared",
    "-Wall",
    "-Wextra",
    "-Wno-unused-parameter",
    "-Wno-missing-field-initializers",
    "-I$(Join-Path $ProjectDir 'src')",
    "-I$(Join-Path $WorkspaceDir 'ModDev_Common')",
    "-DWIN32_LEAN_AND_MEAN",
    "-DNOMINMAX"
)
if ($Debug) { $Flags += @("-O0", "-g", "-DMOD_DEBUG=1") } else { $Flags += @("-O2", "-s") }

$LinkFlags = @(
    "-static",
    "-static-libgcc",
    "-static-libstdc++",
    "-Wl,--kill-at",
    "-luser32",
    "-lgdi32",
    "-lshell32"
)

Write-Host "[BUILD] Compiling $($Sources.Count) source file(s) -> $OutAsi" -ForegroundColor Cyan
& $Compiler @Flags @Sources -o $OutAsi @LinkFlags
if ($LASTEXITCODE -ne 0) { throw "Compilation failed with exit code $LASTEXITCODE" }

$SizeKB = [math]::Round((Get-Item $OutAsi).Length / 1024, 1)
Write-Host "[OK] Built $OutAsi ($SizeKB KB)" -ForegroundColor Green

if (Test-Path $ObjDump) {
    $Header = & $ObjDump -f $OutAsi | Select-String "file format"
    $Imports = & $ObjDump -p $OutAsi | Select-String "DLL Name:"
    Write-Host "[VERIFY] $Header" -ForegroundColor DarkGray
    Write-Host "[VERIFY] Imports: $(($Imports | ForEach-Object { $_.ToString().Trim() }) -join ', ')" -ForegroundColor DarkGray
}

if (-not $NoDeploy) {
    if ([string]::IsNullOrWhiteSpace($ResolvedGameDir) -or -not (Test-Path $ResolvedGameDir)) {
        Write-Warning "[DEPLOY] Skipped: GTA SA folder not configured or not found ('$ResolvedGameDir'). Set `$GameDir in local.config.ps1 or pass -GameDir."
    } else {
        $DeployDir = Join-Path $ResolvedGameDir "modloader\$ModName"
        New-Item -ItemType Directory -Force -Path $DeployDir | Out-Null
        try {
            Copy-Item -Force $OutAsi (Join-Path $DeployDir "$ModName.asi")
        } catch {
            throw "Failed to copy $ModName.asi to '$DeployDir'. Is gta_sa.exe currently running? $_"
        }
        $DefaultIni = Join-Path $ProjectDir "config\$ModName.ini"
        $TargetIni  = Join-Path $DeployDir "$ModName.ini"
        if ((Test-Path $DefaultIni) -and ($OverwriteIni -or (-not (Test-Path $TargetIni)))) {
            Copy-Item -Force $DefaultIni $TargetIni
        }
        Write-Host "[DEPLOY] Installed to $DeployDir" -ForegroundColor Green
    }
}
