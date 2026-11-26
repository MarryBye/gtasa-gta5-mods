# GTA5UIKit — Shared Runtime UI Kit, Addon Bus & Vanilla UI Restyler

Module **A** of the modular ASI suite:
- **Global Theme & Configuration Host:** Synchronizes `Language` (`rus`/`eng` with MSKL Russificator + English support), `SpeedUnit` (`mph`/`kmh`), `UIScale`, and `Coop.ProfileName` across all installed modules (`GTA5GarageAndTuning`, `GTA5AmmuNation`, `GTA5Inventory`, `GTA5Phone`).
- **Inter-Mod Service Bus (`mod_bus.hpp`):** Enables standalone ASI plugins to discover and integrate with one another in any load order with zero static DLL dependencies.
- **Complete Vanilla UI Restyling (`RestyleVanillaMenus=1`, `RestyleHelpBoxes=1`):** Intercepts `CMenuSystem` (`DisplayStandardMenu` / `DisplayGridMenu` / input handlers) and `CHud::DrawHelpText` to restyle barber shops, tattoo parlors, gyms, betting shops, mission menus, color grids, and top-left notifications/prompts/story calls using the shared UI Kit components (`ListMenu`, `ColorGrid`, `HelpToast`) without altering vanilla gameplay logic.
- **Unified Menu Controls:** Applies consistent navigation across all unclaimed vanilla menus (`Wheel` / `Arrows` / `W`/`S` to scroll, `F`/`Enter`/`LMB` to confirm, `Backspace`/`Esc`/`RMB` to go back without opening the Pause Menu).

## Configuration (`GTA5UIKit.ini`)
Configure language, speed units, UI scale, vanilla restyling flags, logging, and optional SA-COOP profile name in [`config/GTA5UIKit.ini`](config/GTA5UIKit.ini).

## Building from Source
Requires 32-bit MinGW-w64 (`C:\msys64\mingw32\bin\g++.exe`, C++20) and sibling `../ModDev_Common`:
```powershell
.\build.ps1            # Build and auto-deploy to <GameDir>\modloader\GTA5UIKit
.\build.ps1 -NoDeploy  # Build only to .\build\GTA5UIKit.asi
```
