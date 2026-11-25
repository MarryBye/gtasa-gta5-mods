param(
    [switch]$NoDeploy,
    [switch]$Debug
)

$ErrorActionPreference = "Stop"

$ProjectDir   = $PSScriptRoot
$GameDir      = Split-Path -Parent $ProjectDir
$BuildDir     = Join-Path $ProjectDir "build"
$DeployDir    = Join-Path $GameDir "modloader\GTA5EarlyUnlock"
$Compiler     = "C:\msys64\mingw32\bin\g++.exe"
$ObjDump      = "C:\msys64\mingw32\bin\objdump.exe"
$OutAsi       = Join-Path $BuildDir "GTA5EarlyUnlock.asi"

if (-not (Test-Path $Compiler)) {
    throw "32-bit compiler not found at $Compiler. Install mingw-w64-i686-gcc via MSYS2."
}

New-Item -ItemType Directory -Force -Path $BuildDir | Out-Null
$env:PATH = "C:\msys64\mingw32\bin;$env:PATH"

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
    "-I$(Join-Path $GameDir 'ModDev_Common')",
    "-DWIN32_LEAN_AND_MEAN",
    "-DNOMINMAX"
)

if ($Debug) {
    $Flags += @("-O0", "-g", "-DMOD_DEBUG=1")
} else {
    $Flags += @("-O2", "-s")
}

$LinkFlags = @(
    "-static",
    "-static-libgcc",
    "-static-libstdc++",
    "-Wl,--kill-at",
    "-luser32",
    "-lshell32"
)

Write-Host "[BUILD] Compiling $($Sources.Count) source file(s) -> $OutAsi" -ForegroundColor Cyan
& $Compiler @Flags @Sources -o $OutAsi @LinkFlags
if ($LASTEXITCODE -ne 0) {
    throw "Compilation failed with exit code $LASTEXITCODE"
}

$SizeKB = [math]::Round((Get-Item $OutAsi).Length / 1024, 1)
Write-Host "[OK] Built $OutAsi ($SizeKB KB)" -ForegroundColor Green

if (Test-Path $ObjDump) {
    $Header  = & $ObjDump -f $OutAsi | Select-String "file format"
    $Imports = & $ObjDump -p $OutAsi | Select-String "DLL Name:"
    Write-Host "[VERIFY] $Header" -ForegroundColor DarkGray
    Write-Host "[VERIFY] Imports: $(($Imports | ForEach-Object { $_.ToString().Trim() }) -join ', ')" -ForegroundColor DarkGray
}

if (-not $NoDeploy) {
    New-Item -ItemType Directory -Force -Path $DeployDir | Out-Null
    Copy-Item -Force $OutAsi (Join-Path $DeployDir "GTA5EarlyUnlock.asi")
    $DefaultIni = Join-Path $ProjectDir "config\GTA5EarlyUnlock.ini"
    $TargetIni  = Join-Path $DeployDir "GTA5EarlyUnlock.ini"
    if ((Test-Path $DefaultIni) -and (-not (Test-Path $TargetIni))) {
        Copy-Item $DefaultIni $TargetIni
    }
    Write-Host "[DEPLOY] Installed to $DeployDir" -ForegroundColor Green
}
