# GTA San Andreas Modular ASI Suite — Developer & Agent Guide

## Project Purpose
A modular suite of standalone **C++20 32-bit ASI plugins** for **GTA San Andreas 1.0 US (Hoodlum)**, built for fun and creative experimentation — porting GTA 5 / ETS 2 mechanics and custom gameplay ideas into classic GTA SA while staying **100% compatible with SA-COOP (`Karimov-Akbar/sa-coop`)**.

## Repository Structure
- `ModDev_Common/` — Header-only shared library (no external SDK required):
  - `sa_sdk.hpp` — Verified GTA SA 1.0 US structures, native game calls, SCM script wrapper, SA-COOP detection.
  - `ui_kit.hpp` — Dumb-component UI library (`ThemeTokens`, `ListMenu`, `StatsCard`, `PhoneFrame`, `RadialWheel`, `DrawTextInputBox`).
  - `mod_bus.hpp` — Load-order-independent pure C-ABI inter-mod service bus & phone addon registry.
  - `bass_audio.hpp` — Dynamic `bass.dll` loader and procedural PCM audio synthesis.
  - `text_rus.hpp` — UTF-8 to SanLtd / MSKL Russificator encoding with GTA color tags.
  - `memory.hpp` — Safe x86 hooking utilities (`HookCall`, `WriteJmp`, `PatchBytes`).
  - `log.hpp` — Thread-safe per-plugin file logger.
- `ModDev_UIKit/` → `GTA5UIKit.asi` (shared UI theme + vanilla menu restyling + ModBus host)
- `ModDev_GTA5Weapons/` → `GTA5AmmuNation.asi` (weapon attachments, custom audio, 8-slot wheel)
- `ModDev_GTA5Garage/` → `GTA5GarageAndTuning.asi` (deep tuning, 10 personal cars, ETS 2 wear, dealerships)
- `ModDev_Inventory/` → `GTA5Inventory.asi` (takeaway snacks & spare body armor)
- `ModDev_Phone/` → `GTA5Phone.asi` (90s phone with contacts, wardrobe, dealership, addon host)
- `ModDev_EarlyUnlock/` → `GTA5EarlyUnlock.asi` (early unlock of shops, workshops, restaurants)

## Toolchain & Build System
- **Compiler:** 32-bit MinGW-w64 (`C:\msys64\mingw32\bin\g++.exe`, C++20, `-m32 -shared -static -static-libgcc -static-libstdc++ -Wl,--kill-at`).
- **Game path configuration:** Copy `local.config.example.ps1` to `local.config.ps1` (gitignored) and set `$GameDir`.
  - Resolution priority: `-GameDir` parameter → `$env:GTASA_DIR` → `local.config.ps1` → legacy `..\modloader`.
- **Commands:**
  - Single plugin (compile + auto-deploy to `<GameDir>\modloader\<ModName>\`):
    ```powershell
    powershell -NoProfile -ExecutionPolicy Bypass -File .\ModDev_UIKit\build.ps1
    ```
  - All plugins (compile + deploy, skip ZIPs for fast iteration):
    ```powershell
    powershell -NoProfile -ExecutionPolicy Bypass -File .\build-all.ps1 -SkipZip
    ```
  - All plugins + release archives in `dist\` + deploy:
    ```powershell
    powershell -NoProfile -ExecutionPolicy Bypass -File .\build-all.ps1 -Version v2.3.1
    ```
  - Selective build via `-Only GTA5Phone,GTA5UIKit`, debug symbols via `-Debug`, skip deploy via `-NoDeploy`, refresh INI defaults via `-OverwriteIni`.
  - Full release (build + deploy to modloader + commit/push + GitHub Release):
    ```powershell
    powershell -NoProfile -ExecutionPolicy Bypass -File .\publish-release.ps1 -Version vX.Y.Z -Title "..." -Notes "..." -CommitMessage "..."
    ```

## Adding a New Mod Module
1. Create `ModDev_<YourMod>/` with `src/`, `config/<ModName>.ini`, `build.ps1`, `build.bat`, and `README.md`.
2. Reuse `ModDev_Common/` headers (`sa_sdk.hpp`, `ui_kit.hpp`, `mod_bus.hpp`, `text_rus.hpp`, `memory.hpp`, `log.hpp`).
3. Register the new module in `build-all.ps1` (`$AllMods`) and update root `README.md`.

## SA-COOP & Stability Rules
- Always verify 32-bit PE output (`pe-i386`) and minimal Windows DLL imports via `objdump` (automatic in `build.ps1`).
- Never use slow-motion/time-scale hooks in menus or wheels.
- Always guard hotkeys against unfocused windows (`IsGameWindowFocused`) and SA-COOP chat/menus (`F2`, `F4`, `F9`, `T`).
- Isolate per-player persistence under SA-COOP (`*_coop_<Nickname>.ini`).
- Guard vehicle deletion (`DELETE_CAR`) when another player is inside, and guard `CClothes::RebuildPlayer` when non-CJ skins are active.
- Keep all Russian strings UTF-8 in source and encode via `text_rus.hpp` for in-game rendering.
