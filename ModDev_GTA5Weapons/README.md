# GTA5AmmuNation

**GTA 5-style weapon customization, custom audio, multi-weapon arsenal & vanilla-styled Weapon Wheel for GTA San Andreas 1.0 US (Hoodlum).**

This ASI plugin ports GTA 5's weapon attachment shop, multi-weapon slot inventory, and radial Weapon Wheel into classic GTA San Andreas styling while remaining 100% compatible with **SA-COOP** and graphic/interface mods.

---

## Key Features

### 1. Revamped Ammu-Nation Shop
- Built entirely on the shared dumb-component UI Kit (`ModDev_Common/ui_kit.hpp`) in GTA SA's classic monochrome aesthetic with automatic text fitting for every resolution and **WidescreenFix**.
- Categories: **Melee**, **Pistols**, **Shotguns**, **SMGs**, **Assault Rifles**, **Sniper Rifles**, **Heavy**, **Thrown**, and **Body Armor** (plus **spare body armor** purchase integration when `GTA5Inventory` is installed).
- Owned weapons are marked in green as **`Owned`**. Re-entering an owned weapon opens its attachment customization and ammo purchase screen.
- **Bottom-Right Stats Card:** Damage, Fire Rate, Accuracy, Range, and Magazine Size — numeric stats, progress bars, and live green `+` / red `−` previews.

### 2. GTA 5-Style Weapon Attachments
- **Muzzle:** *Silencer* (quiet synthesized shot via `bass.dll` + stability) / *Compensator* (higher accuracy + punchy muzzle blast).
- **Magazine:** *Extended Mag* (+60% clip capacity) / *Drum Mag* (+100% clip capacity).
- **Optics:** *Collimator* (+accuracy, light RMB zoom) / *Tactical Scope* (++accuracy & range, smooth RMB camera zoom).
- **Underbarrel:** *Tactical Grip* (improved accuracy + movement stability).
- **Barrel & Trigger:** *Heavy Barrel* (+damage, +range, +fire rate).
- **Tactical Laser Sight:** *Red* or *Green* 3D laser beam aligned to the real aim ray (`Find3rdPersonCamTargetVector`), complete with a glowing dot on target while aiming.
- **Optional 3D Attachments on Weapon Models** (`Enable3DAttachments=0` by default in INI): when enabled (`1`), silencers, compensators, optics, grips, extended/drum mags, and laser modules render directly on the 3D weapon model (including dual-wield Hitman skill).

### 3. Multi-Weapon Category Storage + 8-Sector Weapon Wheel
- CJ can carry **multiple weapons within the same weapon slot** (e.g., **9mm + Desert Eagle**, **AK-47 + M4**, **Micro-SMG + MP5** simultaneously) sharing category ammo, and swap between them anytime.
- **Mouse Wheel:** Opens the compact wheel and quickly cycles through owned weapons.
- **Hold `TAB`:** Detailed 8-sector monochrome GTA SA Weapon Wheel with native HUD weapon icons:
  - Mouse movement (or `1–8`, `W`/`S`, `↑`/`↓`) — select weapon category;
  - `Mouse Wheel` / `Q`/`E` / `←`/`→` — switch weapons within the hovered category;
  - Release `TAB` — equip the highlighted weapon.
- **Zero slow-motion** — 100% safe for **SA-COOP**!

### 4. Per-Save & SA-COOP Persistence
- All owned weapons, attachments, and active per-category selections are stored in `Documents\GTA San Andreas User Files\GTA5WeaponSaves\` (isolated per-player under SA-COOP).
- **New Game** in singleplayer always starts with a clean arsenal.

---

## Configuration (`GTA5AmmuNation.ini`)
Configure language (`rus`/`eng`), 3D attachment rendering (`Enable3DAttachments`), 3D laser sight (`EnableLaserSight`), scope zoom (`EnableScopeZoom`), custom weapon audio (`EnableWeaponAudio`, `AudioVolume`), Weapon Wheel (`EnableWeaponWheel`, `WheelKey`), and all prices in [`config/GTA5AmmuNation.ini`](config/GTA5AmmuNation.ini).

## Building from Source
Requires 32-bit MinGW-w64 (`C:\msys64\mingw32\bin\g++.exe`, C++20) and sibling `../ModDev_Common`:
```powershell
.\build.ps1            # Build and auto-deploy to <GameDir>\modloader\GTA5AmmuNation
.\build.ps1 -NoDeploy  # Build only to .\build\GTA5AmmuNation.asi
```
