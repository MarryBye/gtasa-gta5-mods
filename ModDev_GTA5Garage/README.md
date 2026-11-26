# GTA5GarageAndTuning

**GTA 5-style vehicle tuning, ETS 2-style wear & servicing, dealerships, personal garage & mechanic delivery for GTA San Andreas 1.0 US (Hoodlum).**

This ASI plugin adds deep performance and visual customization across all 5 workshops in San Andreas (inside the native TransFender, Loco Low Co., and Wheel Arch Angels interiors), an ETS 2-style component wear & servicing system, official vehicle dealerships in all 3 cities, a 10-slot personal garage with per-save persistence, and realistic road-network Mechanic delivery (`F6` standalone or integrated into `GTA5Phone`).

---

## Key Features

### 1. GTA 5-Style Vehicle Workshop (All 5 Mod Shops)
- **Open to all land cars and motorcycles** across all 5 locations (TransFender LS/SF/LV, Loco Low Co., Wheel Arch Angels) inside authentic interiors with zero vanilla vehicle restrictions.
- **Performance Tuning** (per-vehicle isolated `tHandlingData` copy — traffic vehicles are never affected):
  - **Engine (EMS Lv. 1–4):** Substantial acceleration and top-speed gains with reduced aerodynamic drag (keeps native engine audio 100% compatible with vehicle sound mods).
  - **Transmission (Street / Sport / Race):** Adds extra gears (up to 5-speed for 3- and 4-speed cars such as the Hermes), extends top gear ratios, speeds up shifts, and displays the **Gears** stat on the stats card.
  - **Suspension (Lowered / Street / Sport / Race):** Visually lowers ride height live inside the workshop while increasing stiffness and anti-dive stability.
  - **Brakes (Street / Sport / Race):** Significantly shortens braking distance.
  - **Tires:** 3 grip tiers + **Bulletproof Tires**.
  - **Turbo:** Extra torque, subtle turbo spool, and blow-off valve flutter on lift-off/shifts (automatically muted while airborne or paused).
  - **Armor / Chassis (20%–100%):** Reduces HP damage (up to −74%), protects detachable panels, and reinforces body rigidity/mass (compatible with *Proper Vehicle Damage v0.3.7* — armored cars deform noticeably less in collisions).
- **Full Visual Customization:**
  - All 17 wheel sets, spoilers, bumpers, side skirts, exhausts, roofs, hoods, bullbars/lights, hydraulics, nitro, bass boost/stereo, paintjobs, primary & secondary colors, and repairs.
  - **Custom License Plate:** Choose presets or type up to 8 custom characters in-game with instant 3D texture updates on the vehicle.
  - **Horns:** 4 synthesized custom horns (truck, sport, classic, musical).
- **Smart Workshop Camera:**
  - Auto-focuses on the currently selected tuning category (front, rear/exhaust/plate, spoiler, side profile, roof).
  - Manual orbit (`Q`/`E` or `←`/`→`), **zoom** (`Mouse Wheel`, `Z`/`X`, `+`/`-`), and focus reset (`C`).
- **Live Stats Card:** Numeric readouts and progress bars (`Top Speed` in MPH or KM/H, `Acceleration`, `Braking`, `Handling`, `Armor`, `Gears`) with live green `+` / red `−` upgrade previews and red ETS 2 wear-loss indicators.

### 2. ETS 2-Style Component Wear & Servicing
- Individual component wear tracking for **engine, transmission, brakes, tires, suspension, and chassis**.
- Accumulated mileage, street-spawn wear, hard collisions, and explosions degrade real vehicle handling and display red wear-loss segments on `StatsCard`; full workshop servicing restores peak performance.

### 3. Official Dealerships (3 Cities)
- Visit **Coutt & Schutz (LS)**, **Wang Cars (SF)**, and **Auto Bahn (LV)** via classic red entry markers (or order vehicles online through `GTA5Phone`) with live 3D showroom preview and stats comparison.

### 4. Personal Garage & Mechanic Delivery (`F6`)
- Register up to **10 personal vehicles** (2 pages × 5 slots) directly inside any workshop or upon dealership purchase.
- **Pay 'n' Spray** repairs and clears wanted stars without repainting registered personal vehicles.
- **Delivery Menu (`F6` or via `GTA5Phone`)** with a live stats card for every owned car (showing installed upgrades, turbo, bulletproof tires, wear, and license plate).
- **Realistic Mechanic Delivery:** When near a road (not off-road or in water), a classic San Andreas mechanic spawns ~100 m away, drives along the road network to your position (with radar blip and anti-stuck handling), parks, and walks away.
- **Insurance:** Destroyed or submerged personal vehicles can be recovered via `F6` / Phone for a configurable fee.

### 5. Saves & Compatibility
- External per-save storage tied to `GTASAsf1..8` inside `Documents\GTA San Andreas User Files\GTA5GarageSaves\` (vanilla `.b` save files are never modified).
- **New Game** in singleplayer starts with a clean empty garage; under **SA-COOP**, each player uses an isolated profile (`*_coop_<Nickname>.ini`).
- Compatible with **Proper Shaders**, **ModelExtras**, **Proper Vehicle Damage**, **Izzy's Lively Vehicle Physics**, **WidescreenFix**, **SA Styled Speedometer**, **MSKL / SanLtd Russificator**, and **SA-COOP**.

---

## Controls

| Action | Keys |
|---|---|
| Open / close delivery menu (standalone) | `F6` (configurable in INI; integrated into `GTA5Phone` when installed) |
| Menu navigation | `Mouse Wheel` / `W`/`S` / `↑`/`↓`, `F`/`Enter`/`LMB` — select, `Backspace`/`Esc`/`RMB` — back |
| Orbit workshop camera | `Q`/`E` or `←`/`→` |
| Zoom workshop camera | `Mouse Wheel`, `Z`/`X`, `+`/`-` |
| Reset camera focus to part | `C` |
| Switch garage pages (`F6`) | `←`/`→` |
| Remove vehicle from garage (`F6`) | `Delete` |

---

## Configuration (`GTA5GarageAndTuning.ini`)
All options (language `rus`/`eng`, speed units `mph`/`kmh`, `F6` keybind, delivery distance, insurance rate, wear & servicing multipliers, turbo/engine audio toggles, and all prices) are configurable in [`config/GTA5GarageAndTuning.ini`](config/GTA5GarageAndTuning.ini).

## Building from Source
Requires 32-bit MinGW-w64 (`C:\msys64\mingw32\bin\g++.exe`, C++20) and sibling `../ModDev_Common`:
```powershell
.\build.ps1            # Build and auto-deploy to <GameDir>\modloader\GTA5GarageAndTuning
.\build.ps1 -NoDeploy  # Build only to .\build\GTA5GarageAndTuning.asi
```
